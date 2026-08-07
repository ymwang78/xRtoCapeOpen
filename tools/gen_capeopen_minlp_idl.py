#!/usr/bin/env python3
"""Regenerate CAPEOPEN100_Minlp.idl from the CAPE-OPEN specification PDFs.

    python tools/gen_capeopen_minlp_idl.py [docs_dir] [out_idl]

Defaults to `docs/CAPE OPEN 1.1/Documentation_set_1.1_Y23-12` and
`CAPEOPEN100_Minlp.idl`, both relative to the repository root.

Requires pypdf.

WHY A GENERATOR AND NOT A HAND-WRITTEN FILE
    ICapeMINLP has 32 operations averaging three arguments each, plus raises
    clauses -- transcribing that from a PDF by hand is a guarantee of errors
    that compile fine and fail on the wire. This script parses the method
    tables in section 3.6 of the Optimisation specification instead, and
    refuses to emit anything if a single argument type or return value cannot
    be resolved. Re-run it and diff to audit the reconstruction, or to redo it
    against a newer document set.

    See the header of the generated .idl for full provenance and for the list
    of known deviations from the official CAPE-OPENv1-0-0.idl.

ASCII-ONLY OUTPUT IS DELIBERATE
    tao_idl preprocesses with cl.exe. On a CP936 console, a UTF-8 fullwidth
    character whose trailing byte pairs with the newline makes the preprocessor
    swallow the line break -- which silently ate `module Types {` and produced
    64 bogus "error in lookup of symbol" diagnostics. Third-party IDL compilers
    are no more forgiving, and this file exists to be handed to third parties.
    The emitter asserts the result is ASCII before writing.
"""
import os
import re
import sys

OPT_PDF = 'Optimisation_Interface_Specification.pdf'

KNOWN_TYPES = ['CapeArrayBoolean', 'CapeArrayLong', 'CapeArrayDouble', 'CapeArrayString',
               'CapeArrayInterface', 'CapeBoolean', 'CapeLong', 'CapeDouble', 'CapeString',
               'CapeInterface', 'CapeVariant', 'CapeMINLPObjFunType']

# Two typos in the specification text, normalised from context. ECapeT imeOut is
# a hard line break in the PDF; ECapeUnkown is a genuine misspelling in the source.
ERR_FIXUPS = {'ECapeT': 'ECapeTimeOut', 'ECapeUnkown': 'ECapeUnknown'}

# Errors Optimisation defines in its own scope rather than reusing Common::Error.
MINLP_OWN_ERRORS = {'ECapeHessianInfoNotAvailable', 'ECapeOutsideSolverScope'}

ERR_COMMON = ['ECapeUnknown', 'ECapeInvalidArgument', 'ECapeOutOfResources', 'ECapeNoMemory',
              'ECapeTimeOut', 'ECapeSolvingError', 'ECapeLicenceError',
              'ECapeFailedInitialisation', 'ECapeBadInvOrder']

T = '::CAPEOPEN100::Common::Types::'
E = '::CAPEOPEN100::Common::Error::'

IFACES = ['ICapeMINLP', 'ICapeMINLPSystem', 'ICapeMINLPSolverManager']

# 每个 CO 业务接口都继承 ICapeIdentification。依据（均在 docs/ 下）：
#
#   Methods&Tools_Integrated_Guidelines.pdf 9.2.2:
#     "All PMC objects (primary and secondary) have to provide Common Interfaces
#      services to any PME such as an identification process and an error
#      handling strategy."  —— 用例图里 "Identifying PMC Object" 对 primary 和
#      secondary 都标为 mandatory。
#
#   同文 6.1.2 (CORBA):
#     "the interface diagram can map the inheritance relationships directly into
#      the CIDL"  —— 与 6.1.1 (COM) 明确相反："write the MIDL assuming an
#      implementation that is not done with custom interface inheritance"。
#      也就是说 COM 靠 QueryInterface，CORBA 靠 IDL 继承。
#
#   同文附的 CIDL 实例：interface ICapeThermoSystem : Cape::ICapeIdentification
#
# 注意 Optimisation 规范本身**从未提及** Identification，其 Figure 2 也没画这层
# 继承。所以这一条是按通用规则推出的，不是 Optimisation 规范的明文。
#
# 之所以仍然加上，是因为风险不对称：官方 IDL 若有继承而我们没有，PME 就拿不到
# 标识——那是强制要求；官方若没有而我们加了，代价只是多 4 个方法、_is_a 多答
# 一个 true，无害。继承也不改变 ICapeMINLP 自身的 Repository ID。
IDENTIFICATION_BASE = '::CAPEOPEN100::Common::Identification::ICapeIdentification'


def pdf_text(path):
    import pypdf
    reader = pypdf.PdfReader(path)
    return '\n'.join((page.extract_text() or '') for page in reader.pages)


def norm_type(raw):
    squashed = re.sub(r'\s+', '', raw)
    for t in sorted(KNOWN_TYPES, key=len, reverse=True):
        if squashed.startswith(t):
            return t
    return None


def parse_methods(text):
    """Parse the section 3.6 method tables into [{iface, method, args, errs}]."""
    lines = text.splitlines()

    # 这两个边界是靠 PDF 抽出来的正文标题定位的，而抽文结果会随 pypdf 版本、
    # 文档版本、甚至字距而变。找不到时裸 StopIteration 的 traceback 说明不了
    # 任何事——这个生成器的整体设计就是「解析不出来就明确拒绝」，边界定位没有
    # 理由例外。
    def find(predicate, what):
        idx = next((i for i, l in enumerate(lines) if predicate(l.strip())), None)
        if idx is None:
            raise RuntimeError(
                f'cannot locate {what} in the extracted text of {OPT_PDF}. '
                f'The heading may have changed, or pypdf may be extracting it '
                f'differently -- dump the text and adjust the boundary predicate.')
        return idx

    start = find(lambda s: s == '3.6 Interface descriptions', 'the start of section 3.6')
    end = find(lambda s: s.startswith('3.7 Scenarios'), 'the start of section 3.7')

    methods, cur, mode = [], None, None
    for line in lines[start:end]:
        t = line.strip()
        if t.startswith('Interface Name '):
            if cur:
                methods.append(cur)
            cur = {'iface': t[15:].strip(), 'method': None, 'returns': None,
                   'args': [], 'errs': []}
            mode = None
            continue
        if cur is None:
            continue
        if t.startswith('Method Name '):
            cur['method'] = t[12:].strip()
            continue
        if t.startswith('Returns') and cur['returns'] is None:
            # First occurrence only: several Description rows also open with
            # "Returns the value of ...", which would otherwise overwrite it.
            cur['returns'] = t[7:].strip()
            continue
        if t == 'Arguments':
            mode = 'args'
            continue
        if t == 'Errors':
            mode = 'errs'
            continue
        if mode == 'args':
            m = re.match(r'^\[(in|out)\]\s+(\w+)\s+(.*)$', t)
            if m:
                cur['args'].append([m.group(1), m.group(2), norm_type(m.group(3)), m.group(3)])
            elif cur['args'] and cur['args'][-1][2] is None:
                # A type name split across a PDF line break; glue and retry.
                cur['args'][-1][3] += t
                cur['args'][-1][2] = norm_type(cur['args'][-1][3])
        elif mode == 'errs':
            cur['errs'].append(t)
    if cur:
        methods.append(cur)

    methods = [m for m in methods if m['method']]
    for m in methods:
        names = [re.sub(r'\s+', '', n) for n in re.findall(r'EC\s*ape\s*\w+', ' '.join(m['errs']))]
        seen, out = set(), []
        for n in names:
            n = ERR_FIXUPS.get(n, n)
            if n not in seen:
                seen.add(n)
                out.append(n)
        m['errs'] = out
    return methods


def check(methods):
    """Refuse to emit on anything unresolved -- a bad parse must never reach the file."""
    bad = []
    for m in methods:
        for _, n, t, raw in m['args']:
            if t is None:
                bad.append(f"{m['method']}: argument {n} has unresolved type {raw!r}")
        if m['returns'] != '--' and not (m['iface'] == 'ICapeMINLPSystem'
                                         and m['method'] == 'GetParameters'):
            bad.append(f"{m['method']}: unexpected Returns {m['returns']!r}")
    known = set(ERR_COMMON) | MINLP_OWN_ERRORS
    for m in methods:
        for n in m['errs']:
            if n not in known:
                bad.append(f"{m['method']}: unknown exception {n!r}")
    return bad


def qualify(t):
    # Never alias locally: a typedef inside Minlp would mint a Repository ID under
    # Minlp that the official IDL does not have. CapeMINLPObjFunType is genuinely
    # Minlp's own, so it stays bare.
    return t if t == 'CapeMINLPObjFunType' else T + t


def operation(m):
    args = ', '.join(f'{d} {qualify(t)} {n}' for d, n, t, _ in m['args'])
    ret = qualify('CapeInterface') if (m['iface'] == 'ICapeMINLPSystem'
                                       and m['method'] == 'GetParameters') else 'void'
    errs = [n if n in MINLP_OWN_ERRORS else E + n for n in m['errs']]
    head = f'        {ret} {m["method"]}({args})'
    if not errs:
        return head + ';'
    return head + '\n            raises(' + ',\n                   '.join(errs) + ');'


HEADER = '''// ============================================================================
//  CAPEOPEN100_Minlp.idl
//  --------------------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//
//  GENERATED by tools/gen_capeopen_minlp_idl.py -- do not edit by hand.
//  Re-run that script to regenerate and diff.
// ============================================================================
//  A reconstruction of the MINLP subset of the CAPE-OPEN v1.0 CORBA IDL.
//
//  WHY THIS FILE EXISTS
//    The official CAPE-OPENv1-0-0.idl is not in this repository; docs/ holds
//    only the specification PDFs. In CORBA the identity of an interface is its
//    Repository ID -- the counterpart of a COM IID -- and a third-party client
//    narrows with _is_a("IDL:CAPEOPEN100/..."). A servant built from a homegrown
//    module (module SqpSolver, in SqpSolver.idl) is rejected by _narrow no
//    matter how well its method signatures match. The module path has to be
//    right, and that is what this file reconstructs.
//
//  PROVENANCE (every source is under docs/ and can be re-checked)
//
//    Module nesting CAPEOPEN100::{Common::{Types,Error,Identification},
//                                 Business::Numeric::Minlp}
//      Methods&Tools_Integrated_Guidelines.pdf, section "IMPLEMENTATION
//      SPECIFICATION FOR CORBA PLATFORM" -- that section reproduces the module
//      skeleton of the official .idl inline. Corroborated by
//      Optimisation_Interface_Specification.pdf section 4.2 (p.57), "You can
//      get these instructions in CAPE-OPENv1-0-0.idl within the
//      CAPEOPEN100::Business::Numeric::Minlp module", and by
//      Identification Common Interface.pdf section 4.2.
//
//    No #pragma prefix and no #pragma version
//      Same document, verbatim: "The OMG directive #pragma version allows
//      giving a version number to the repository id. It isn't used here." No
//      prefix pragma appears anywhere. The Repository IDs therefore follow
//      directly from the module path, at version 1.0:
//        IDL:CAPEOPEN100/Business/Numeric/Minlp/ICapeMINLP:1.0
//        IDL:CAPEOPEN100/Common/Identification/ICapeIdentification:1.0
//
//    module Common::Types
//      Methods&Tools_Integrated_Guidelines.pdf. This module is reproduced in
//      full there (not elided), so it is transcribed rather than inferred.
//
//    module Common::Identification
//      Identification Common Interface.pdf, section 3.5 method tables.
//
//    module Business::Numeric::Minlp (35 operations)
//      Optimisation_Interface_Specification.pdf, sections 3.6.1 / 3.6.2 /
//      3.6.3 method tables. Each operation's [in]/[out] directions, types and
//      raises clause were parsed out of that text by the generator.
//
//  KNOWN DEVIATIONS -- check these against the official .idl when it arrives
//
//    1. The exception *members* in module Common::Error are reconstructed. The
//       Error specification section 3.3 gives the conceptual attributes
//       (ECapeRoot.name plus ECapeUser.{code, description, scope,
//       interfaceName, operation, moreInfo}) but not the exact CORBA IDL form.
//       Blast radius: a raises clause takes no part in the Repository ID and
//       does not affect GIOP encoding of successful calls; it only matters when
//       an exception is actually thrown, where a third-party client would fail
//       to decode our exception body.
//
//    2. The UNDEFINED constants of Common::Types are omitted. The document
//       writes them in mathematical notation (e.g. -2^31), which is not legal
//       IDL, and Minlp does not use them. Restore them from the official file.
//
//    3. Two typos in the specification text are normalised here from context:
//       ECapeUnkown -> ECapeUnknown, and ECapeT imeOut (a hard line break in
//       the PDF) -> ECapeTimeOut.
//
//    4. GetMINLPHessianStructure takes rowindex as [in] and columnindex as
//       [out]. The asymmetry looks wrong but matches the specification, so it
//       is transcribed unchanged.
//
//    5. The specification mentions "an Array type is also defined to hold
//       sequences of these types" (of CapeMINLPObjFunType), but no method table
//       uses one, so none is invented here. Same for CapeArrayArrayLong, which
//       appears only in a list of types.
//
//    6. The three interfaces inherit ICapeIdentification. This follows a general
//       rule rather than the Optimisation specification, which never mentions
//       Identification at all and does not draw the inheritance in its Figure 2.
//       The rule: Methods&Tools_Integrated_Guidelines.pdf 9.2.2 makes an
//       identification process mandatory for every PMC object, primary and
//       secondary alike; its 6.1.2 says CORBA maps the inheritance relationships
//       directly into the CIDL, in explicit contrast with 6.1.1 for COM ("not
//       done with custom interface inheritance" -- COM uses QueryInterface); and
//       the same document carries the CIDL example
//       "interface ICapeThermoSystem : Cape::ICapeIdentification".
//       Kept because the risk is asymmetric: if the official IDL has the
//       inheritance and this file omits it, a PME cannot obtain identification,
//       which is a mandatory requirement; if the official IDL omits it and this
//       file has it, the cost is four extra operations and one extra _is_a
//       answer. Inheritance does not change ICapeMINLP's own Repository ID
//       either way.
//
//  This file is ASCII-only on purpose, comments included -- see the generator
//  docstring for the preprocessor reason.
// ============================================================================
#ifndef CAPEOPEN100_Minlp_idl
#define CAPEOPEN100_Minlp_idl

module CAPEOPEN100 {

  module Common {

    // Source: Methods&Tools_Integrated_Guidelines.pdf (reproduced in full there)
    module Types {

      typedef long    CapeLong;
      typedef short   CapeShort;
      typedef double  CapeDouble;
      typedef float   CapeFloat;
      typedef boolean CapeBoolean;
      typedef char    CapeChar;
      typedef string  CapeString;
      typedef string  CapeDate;
      typedef string  CapeURL;
      typedef any     CapeVariant;
      typedef Object  CapeInterface;

      typedef sequence<CapeLong>      CapeArrayLong;
      typedef sequence<CapeShort>     CapeArrayShort;
      typedef sequence<CapeDouble>    CapeArrayDouble;
      typedef sequence<CapeFloat>     CapeArrayFloat;
      typedef sequence<CapeChar>      CapeArrayChar;
      typedef sequence<CapeString>    CapeArrayString;
      typedef sequence<CapeBoolean>   CapeArrayBoolean;
      typedef sequence<CapeDate>      CapeArrayDate;
      typedef sequence<CapeURL>       CapeArrayURL;
      typedef sequence<CapeVariant>   CapeArrayVariant;
      typedef sequence<CapeInterface> CapeArrayInterface;

      enum CapeValidationStatus {
        CAPE_NOT_VALIDATED,
        CAPE_INVALID,
        CAPE_VALID
      };
      typedef sequence<CapeValidationStatus> CapeArrayValidationStatus;

      // The UNDEFINED constants are omitted; see deviation 2 in the file header.

    }; // END Types

    // Source: Error Common Interface.pdf section 3.3. Members are reconstructed
    // -- see deviation 1 in the file header. CORBA exceptions do not support
    // inheritance (that specification says so in section 5.2.1), so each
    // exception repeats the full member set.
    module Error {
'''

FOOTER = '''
    }; // END Error

    // Source: Identification Common Interface.pdf section 3.5
    module Identification {

      interface ICapeIdentification {
        ::CAPEOPEN100::Common::Types::CapeString GetComponentName()
            raises(::CAPEOPEN100::Common::Error::ECapeUnknown);
        ::CAPEOPEN100::Common::Types::CapeString GetComponentDescription()
            raises(::CAPEOPEN100::Common::Error::ECapeUnknown);
        void SetComponentName(in ::CAPEOPEN100::Common::Types::CapeString name)
            raises(::CAPEOPEN100::Common::Error::ECapeUnknown,
                   ::CAPEOPEN100::Common::Error::ECapeInvalidArgument);
        void SetComponentDescription(in ::CAPEOPEN100::Common::Types::CapeString desc)
            raises(::CAPEOPEN100::Common::Error::ECapeUnknown,
                   ::CAPEOPEN100::Common::Error::ECapeInvalidArgument);
      };

    }; // END Identification

  }; // END Common

  module Business {
    module Numeric {

      // Source: Optimisation_Interface_Specification.pdf section 3.6
      module Minlp {

        // Specification section 3.6 preamble: "new enumerated data types were
        // defined to represent valid types of objective function:
        // CapeMINLPObjFunType = {MAX, MIN}"
        enum CapeMINLPObjFunType { MAX, MIN };

        // Errors owned by Optimisation. The specification allows a CO interface
        // to define its own errors within its own scope.
        exception ECapeHessianInfoNotAvailable {
          ::CAPEOPEN100::Common::Types::CapeString description;
        };
        exception ECapeOutsideSolverScope {
          ::CAPEOPEN100::Common::Types::CapeString description;
        };

'''

TAIL = '''      }; // END Minlp

    }; // END Numeric
  }; // END Business

}; // END CAPEOPEN100

#endif // CAPEOPEN100_Minlp_idl
'''


def emit(methods):
    by = {}
    for m in methods:
        by.setdefault(m['iface'], []).append(m)

    parts = [HEADER]
    members = ''.join(f'        {T}{t:<9} {n};\n' for t, n in [
        ('CapeString', 'name'), ('CapeLong', 'code'), ('CapeString', 'description'),
        ('CapeString', 'scope'), ('CapeString', 'interfaceName'),
        ('CapeString', 'operation'), ('CapeURL', 'moreInfo')])
    for e in ERR_COMMON:
        parts.append(f'      exception {e} {{\n{members}      }};\n')
    parts.append(FOOTER)
    parts.append('        // The operations below were generated from the section 3.6 method\n'
                 '        // tables. Types are spelled out fully rather than aliased locally:\n'
                 '        // a typedef here would mint Repository IDs under Minlp that the\n'
                 '        // official IDL does not have.\n\n')
    for iface in IFACES:
        parts.append(f'      interface {iface} : {IDENTIFICATION_BASE} {{\n')
        for m in by[iface]:
            parts.append(operation(m) + '\n')
        parts.append('      };\n\n')
    parts.append(TAIL)
    return ''.join(parts), by


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    docs = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        root, 'docs', 'CAPE OPEN 1.1', 'Documentation_set_1.1_Y23-12')
    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, 'CAPEOPEN100_Minlp.idl')

    pdf = os.path.join(docs, OPT_PDF)
    if not os.path.isfile(pdf):
        sys.exit(f'not found: {pdf}\n'
                 f'pass the documentation set directory as the first argument')

    methods = parse_methods(pdf_text(pdf))
    bad = check(methods)
    if bad:
        print('refusing to emit -- unresolved items:', file=sys.stderr)
        for b in bad:
            print('   ', b, file=sys.stderr)
        sys.exit(1)

    text, by = emit(methods)
    offenders = [(i + 1, l) for i, l in enumerate(text.splitlines())
                 if any(ord(c) > 127 for c in l)]
    if offenders:
        print('refusing to emit -- output is not ASCII-only:', file=sys.stderr)
        for i, l in offenders[:10]:
            print(f'    line {i}: {l!r}', file=sys.stderr)
        sys.exit(1)

    with open(out_path, 'w', encoding='ascii', newline='\n') as fp:
        fp.write(text)
    counts = ', '.join(f'{k}={len(v)}' for k, v in by.items())
    print(f'wrote {out_path}: {len(text.splitlines())} lines, '
          f'{sum(len(v) for v in by.values())} operations ({counts})')


if __name__ == '__main__':
    main()
