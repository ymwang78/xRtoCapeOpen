#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Regenerate CAPEOPEN100_Unit.idl -- the Unit Operation / Collection / Parameter
subset of the CAPE-OPEN CORBA IDL.

Companion to gen_capeopen_minlp_idl.py. Same problem, same discipline: the
official CAPE-OPENv1-0-0.idl is not obtainable (see PROVENANCE below), so the
IDL is reconstructed from artefacts that are, and the reconstruction is
cross-checked against one that CO-LaN itself produces.

Usage:
    python tools/gen_capeopen_unit_idl.py [<docs dir>] [<output .idl>]

The type-library cross-check is skipped (with a warning) when the CAPE-OPEN
type libraries are not installed, so the script still runs on Linux/CI.
"""
import os
import sys

# ---------------------------------------------------------------------------
#  PROVENANCE
# ---------------------------------------------------------------------------
#  1. The official CORBA IDL is NOT available (re-checked 2026-09).
#     CO-LaN's "CAPE-OPEN IDL" project download area carries exactly four
#     items -- Documentation_set_1.0 / 1.1 zips, the TLB+PIA installer bundle,
#     and a TLB developer guide. No .idl. The Trac source browser is 403
#     without a login. This is the same finding as issue #3 in
#     docs/xOptMINLPco_design.md section 6.6.
#
#     Note the decoy: docs/.../07_CO_Sequential_Modular_Specific_Tools.zip
#     DOES contain three .idl files (box/idl/*.idl). They are NOT usable as a
#     source -- box/idl/notes.txt says they come from "proto v5" and are
#     "non normalises CO", and the content confirms it: no modules at all,
#     CapeDoubleSequence instead of CapeArrayDouble, CapeUNKNOWN instead of
#     ECapeUnknown. Historical prototype, not the standard.
#
#  2. Module nesting -- Methods&Tools_Integrated_Guidelines.pdf pages 56-60
#     reproduces the official IDL's module skeleton verbatim, comments and all:
#
#         module CAPEOPEN100 {
#           module Common { Types, Error, Identification, Collection,
#                           Utilities, Parameter, Persistence }
#           module Cose   { SContext }
#           module Business {
#             module PhyProp { Thrm{Cose,ThermoSystem,CalculationRoutine,
#                                   EquilibriumServer}, Reactions, Ppdb }
#             module Numeric { Solvers{Eso,PdaEso,Model,Solver}, Minlp, Pedr }
#             module UnitOp  { Unit }
#             module Other   { Smst, Psp }
#           }
#         }
#
#     So the Repository IDs follow (no #pragma prefix, no #pragma version --
#     same document, and the same reasoning as the Minlp file):
#         IDL:CAPEOPEN100/Business/UnitOp/Unit/ICapeUnit:1.0
#         IDL:CAPEOPEN100/Common/Collection/ICapeCollection:1.0
#         IDL:CAPEOPEN100/Common/Parameter/ICapeParameter:1.0
#     This page also *confirms* the Minlp file's Business::Numeric::Minlp.
#
#  3. Operations -- the "Method Name" / "Returns" / "Arguments" / "Errors" rows
#     of the method tables, page numbers recorded per operation below:
#         CO_Unit_Operations_v6.25.pdf     pp.74-90  (Unit, Port, Report,
#                                                     PortVariables)
#         Collection Common Interface.pdf  pp.14-15  (Item, Count)
#         Parameter Common Interface.pdf   pp.24-42  (Parameter + 5 specs)
#         Utilities CommonInterface.pdf    pp.18-20  (Utilities)
#
#  4. Cross-check -- CAPE-OPENv1-0-0.tlb, the type library CO-LaN compiles and
#     ships (download 17, installed to
#     %CommonProgramFiles%\CAPE-OPEN\Type Libraries\). Its typelib name is
#     CAPEOPEN100 and it carries all of these interfaces. check_against_tlb()
#     below asserts that every operation emitted here maps to a member of the
#     corresponding COM interface and that no COM member is left uncovered.
#     The COM binding spells attributes as properties where the specification
#     spells them as Get*/Set* operations, so the mapping is explicit.
#
#  KNOWN DEVIATIONS -- check these against the official .idl when it arrives
#
#    a. **The raises clauses are the specification's "Errors" rows, but the
#       specification is not exhaustive about them** -- many rows read "No
#       specific error", which cannot mean an operation can raise nothing at
#       all (any CORBA operation can raise system exceptions, and the Error
#       spec expects ECapeUnknown as the catch-all). Where a table says "No
#       specific error" this file emits ECapeUnknown alone. That is a judgement
#       call, not a transcription.
#
#    b. ICapeParameter::ValStatus -- the specification's Returns column says
#       CapeParamValStatus, a type that appears nowhere else in any document
#       and is not in the type library. The type library says
#       CapeValidationStatus. The type library is followed, as in the Minlp
#       file's TLB_ARG_OVERRIDES.
#
#    c. ICapeIdentification inheritance is applied to ICapeCollection,
#       ICapeParameter, ICapeParameterSpec, ICapeUnit, ICapeUnitPort and
#       ICapeUnitReport. Same general rule and same asymmetric-risk argument as
#       docs/xOptMINLPco_design.md section 6.3 step 3; it does not change any
#       interface's own Repository ID.
#
#    e. ICapeArrayParameterSpec::ItemsSpecifications -- the specification's
#       method table (p.36) prints it singular, the type library plural. The
#       type library is followed; the cross-check below refuses to emit if that
#       ever stops matching, so the correction cannot quietly revert.
#
#    d. CapePortType / CapePortDirection are emitted inside
#       Business::UnitOp::Unit, and CapeParamType / CapeParamMode inside
#       Common::Parameter, because that is where their defining specification
#       is scoped. If the official file hoists them into Common::Types, their
#       Repository IDs differ -- the interfaces' do not.
#
#  ASCII-only on purpose, comments included: tao_idl preprocesses with cl.exe,
#  and under CP936 the trailing byte of a UTF-8 full-width character pairs with
#  the newline and swallows the following line. Same reason as the Minlp file.
# ---------------------------------------------------------------------------

T = '::CAPEOPEN100::Common::Types::'
E = '::CAPEOPEN100::Common::Error::'
ID = '::CAPEOPEN100::Common::Identification::ICapeIdentification'
PARAM = '::CAPEOPEN100::Common::Parameter::'

UNKNOWN = ['ECapeUnknown']


def op(name, ret, args, errors, page):
    """One operation. args: list of (direction, type, name)."""
    return {'name': name, 'ret': ret, 'args': args, 'errors': errors, 'page': page}


# --- Common::Collection (Collection Common Interface.pdf) -------------------
COLLECTION = [
    op('Item', T + 'CapeInterface',
       [('in', T + 'CapeVariant', 'id')],
       ['ECapeUnknown', 'ECapeInvalidArgument'], 14),
    op('Count', T + 'CapeLong', [], UNKNOWN, 15),
]

# --- Common::Utilities (Utilities CommonInterface.pdf) ---------------------
UTILITIES = [
    op('GetParameters', T + 'CapeInterface', [], UNKNOWN, 18),
    # 'context' is an OMG IDL keyword (the context clause), so the argument is
    # spelled simulationContext here. Same for 'component' in
    # ICapeUnitPortVariables below -- IDL3 reserves it. Argument names do not
    # affect the Repository ID or the wire format, only the generated stubs.
    op('SetSimulationContext', 'void',
       [('in', T + 'CapeInterface', 'simulationContext')], UNKNOWN, 19),
    op('Initialize', 'void', [], UNKNOWN, 20),
    op('Terminate', 'void', [], UNKNOWN, 20),
    op('Edit', 'void', [], UNKNOWN, 20),
]

# --- Common::Parameter (Parameter Common Interface.pdf) --------------------
PARAMETER_ENUMS = [
    # Values and order from the type library (eCapeParamType / eCapeParamMode).
    ('CapeParamType', ['CAPE_REAL', 'CAPE_INT', 'CAPE_OPTION', 'CAPE_BOOLEAN',
                       'CAPE_ARRAY']),
    ('CapeParamMode', ['CAPE_INPUT', 'CAPE_OUTPUT', 'CAPE_INPUT_OUTPUT']),
]

PARAMETER = [
    op('Specification', T + 'CapeInterface', [], UNKNOWN, 24),
    # Deviation (b): specification says CapeParamValStatus, TLB says
    # CapeValidationStatus.
    op('ValStatus', T + 'CapeValidationStatus', [], UNKNOWN, 25),
    op('GetMode', 'CapeParamMode', [], UNKNOWN, 26),
    op('SetMode', 'void', [('in', 'CapeParamMode', 'mode')],
       ['ECapeUnknown', 'ECapeInvalidArgument'], 27),
    op('Validate', T + 'CapeBoolean',
       [('out', T + 'CapeString', 'message')], UNKNOWN, 28),
    op('GetValue', T + 'CapeVariant', [], UNKNOWN, 28),
    op('SetValue', 'void', [('in', T + 'CapeVariant', 'value')],
       ['ECapeUnknown', 'ECapeInvalidArgument'], 29),
    op('Reset', 'void', [], UNKNOWN, 29),
]

PARAMETER_SPEC = [
    op('Type', 'CapeParamType', [], UNKNOWN, 30),
    op('Dimensionality', T + 'CapeVariant', [], UNKNOWN, 31),
]

REAL_SPEC = [
    op('DefaultValue', T + 'CapeDouble', [], UNKNOWN, 33),
    op('LowerBound', T + 'CapeDouble', [], UNKNOWN, 33),
    op('UpperBound', T + 'CapeDouble', [], UNKNOWN, 34),
    op('Validate', T + 'CapeBoolean',
       [('in', T + 'CapeDouble', 'value'), ('out', T + 'CapeString', 'message')],
       UNKNOWN, 34),
]

INTEGER_SPEC = [
    op('DefaultValue', T + 'CapeLong', [], UNKNOWN, 37),
    op('LowerBound', T + 'CapeLong', [], UNKNOWN, 38),
    op('UpperBound', T + 'CapeLong', [], UNKNOWN, 38),
    op('Validate', T + 'CapeBoolean',
       [('in', T + 'CapeLong', 'value'), ('out', T + 'CapeString', 'message')],
       UNKNOWN, 39),
]

BOOLEAN_SPEC = [
    op('DefaultValue', T + 'CapeBoolean', [], UNKNOWN, 41),
    op('Validate', T + 'CapeBoolean',
       [('in', T + 'CapeBoolean', 'value'), ('out', T + 'CapeString', 'message')],
       UNKNOWN, 42),
]

OPTION_SPEC = [
    op('DefaultValue', T + 'CapeString', [], UNKNOWN, 39),
    op('OptionList', T + 'CapeArrayString', [], UNKNOWN, 40),
    op('RestrictedToList', T + 'CapeBoolean', [], UNKNOWN, 40),
    op('Validate', T + 'CapeBoolean',
       [('in', T + 'CapeString', 'value'), ('out', T + 'CapeString', 'message')],
       UNKNOWN, 41),
]

ARRAY_SPEC = [
    op('NumDimensions', T + 'CapeLong', [], UNKNOWN, 35),
    op('Size', T + 'CapeArrayLong', [], UNKNOWN, 35),
    # Specification p.36 prints 'ItemsSpecification'; the type library says
    # 'ItemsSpecifications'. Same class of transcription slip as the three the
    # Minlp generator corrects, and the same rule applies -- the artefact CO-LaN
    # compiles wins over the printed table. Deviation (e).
    op('ItemsSpecifications', T + 'CapeArrayInterface', [], UNKNOWN, 36),
    op('Validate', T + 'CapeArrayBoolean',
       [('in', T + 'CapeVariant', 'value'), ('out', T + 'CapeString', 'message')],
       UNKNOWN, 37),
]

# --- Business::UnitOp::Unit (CO_Unit_Operations_v6.25.pdf) -----------------
UNIT_ENUMS = [
    # Values and order from the type library (eCapePortType / eCapePortDirection).
    # The specification notes (pp.79-80) that CAPE_ANY and CAPE_INLET_OUTLET
    # are "reserved for future use and should not be used" -- they are still
    # part of the type, so they are emitted.
    ('CapePortType', ['CAPE_MATERIAL', 'CAPE_ENERGY', 'CAPE_INFORMATION',
                      'CAPE_ANY']),
    ('CapePortDirection', ['CAPE_INLET', 'CAPE_OUTLET', 'CAPE_INLET_OUTLET']),
]

UNIT = [
    op('GetValStatus', T + 'CapeValidationStatus', [], UNKNOWN, 74),
    op('Calculate', 'void', [],
       ['ECapeUnknown', 'ECapeSolvingError', 'ECapeBadCOParameter',
        'ECapeOutOfResources', 'ECapeTimeOut'], 76),
    op('GetPorts', T + 'CapeInterface', [], UNKNOWN, 77),
    op('Validate', T + 'CapeBoolean', [('out', T + 'CapeString', 'message')],
       ['ECapeUnknown', 'ECapeBadCOParameter'], 78),
]

UNIT_PORT = [
    op('GetPortType', 'CapePortType', [], UNKNOWN, 79),
    op('GetDirection', 'CapePortDirection', [], UNKNOWN, 80),
    op('GetConnectedObject', T + 'CapeInterface', [], UNKNOWN, 81),
    op('Connect', 'void', [('in', T + 'CapeInterface', 'objectToConnect')],
       ['ECapeUnknown', 'ECapeInvalidArgument'], 82),
    op('Disconnect', 'void', [], UNKNOWN, 83),
]

UNIT_REPORT = [
    op('GetSelectedReport', T + 'CapeString', [], UNKNOWN, 84),
    op('SetSelectedReport', 'void', [('in', T + 'CapeString', 'report')],
       ['ECapeUnknown', 'ECapeInvalidArgument'], 85),
    op('GetReports', T + 'CapeArrayString', [], UNKNOWN, 86),
    op('ProduceReport', 'void', [('inout', T + 'CapeString', 'message')],
       ['ECapeUnknown', 'ECapeBadInvOrder'], 87),
]

PORT_VARIABLES = [
    op('SetIndex', 'void',
       [('in', T + 'CapeString', 'variableType'),
        ('in', T + 'CapeString', 'componentName'),
        ('in', T + 'CapeLong', 'index')],
       ['ECapeUnknown', 'ECapeInvalidArgument'], 88),
    op('GetIndex', T + 'CapeLong',
       [('in', T + 'CapeString', 'variableType'),
        ('in', T + 'CapeString', 'componentName')],
       ['ECapeUnknown', 'ECapeInvalidArgument'], 89),
]

# Interfaces to emit: (module path, name, base, operations)
INTERFACES = [
    ('Common::Collection', 'ICapeCollection', ID, COLLECTION),
    ('Common::Utilities', 'ICapeUtilities', None, UTILITIES),
    ('Common::Parameter', 'ICapeParameterSpec', ID, PARAMETER_SPEC),
    ('Common::Parameter', 'ICapeRealParameterSpec', None, REAL_SPEC),
    ('Common::Parameter', 'ICapeIntegerParameterSpec', None, INTEGER_SPEC),
    ('Common::Parameter', 'ICapeBooleanParameterSpec', None, BOOLEAN_SPEC),
    ('Common::Parameter', 'ICapeOptionParameterSpec', None, OPTION_SPEC),
    ('Common::Parameter', 'ICapeArrayParameterSpec', None, ARRAY_SPEC),
    ('Common::Parameter', 'ICapeParameter', ID, PARAMETER),
    ('Business::UnitOp::Unit', 'ICapeUnit', ID, UNIT),
    ('Business::UnitOp::Unit', 'ICapeUnitPort', ID, UNIT_PORT),
    ('Business::UnitOp::Unit', 'ICapeUnitReport', ID, UNIT_REPORT),
    ('Business::UnitOp::Unit', 'ICapeUnitPortVariables', None, PORT_VARIABLES),
]

# COM spelling of each operation, for the type-library cross-check.
# Left: the specification's operation name emitted above.
# Right: the member as the COM binding spells it (propget/propput collapse).
TLB_NAME_MAP = {
    'ICapeCollection': {'Item': 'Item', 'Count': 'Count'},
    'ICapeUtilities': {'GetParameters': 'parameters',
                       'SetSimulationContext': 'simulationContext',
                       'Initialize': 'Initialize', 'Terminate': 'Terminate',
                       'Edit': 'Edit'},
    'ICapeParameterSpec': {'Type': 'Type', 'Dimensionality': 'Dimensionality'},
    'ICapeRealParameterSpec': {'DefaultValue': 'DefaultValue',
                               'LowerBound': 'LowerBound',
                               'UpperBound': 'UpperBound',
                               'Validate': 'Validate'},
    'ICapeIntegerParameterSpec': {'DefaultValue': 'DefaultValue',
                                  'LowerBound': 'LowerBound',
                                  'UpperBound': 'UpperBound',
                                  'Validate': 'Validate'},
    'ICapeBooleanParameterSpec': {'DefaultValue': 'DefaultValue',
                                  'Validate': 'Validate'},
    'ICapeOptionParameterSpec': {'DefaultValue': 'DefaultValue',
                                 'OptionList': 'OptionList',
                                 'RestrictedToList': 'RestrictedToList',
                                 'Validate': 'Validate'},
    'ICapeArrayParameterSpec': {'NumDimensions': 'NumDimensions',
                                'Size': 'Size',
                                'ItemsSpecifications': 'ItemsSpecifications',
                                'Validate': 'Validate'},
    'ICapeParameter': {'Specification': 'Specification', 'ValStatus': 'ValStatus',
                       'GetMode': 'Mode', 'SetMode': 'Mode',
                       'Validate': 'Validate', 'GetValue': 'value',
                       'SetValue': 'value', 'Reset': 'Reset'},
    'ICapeUnit': {'GetValStatus': 'ValStatus', 'Calculate': 'Calculate',
                  'GetPorts': 'ports', 'Validate': 'Validate'},
    'ICapeUnitPort': {'GetPortType': 'portType', 'GetDirection': 'direction',
                      'GetConnectedObject': 'connectedObject',
                      'Connect': 'Connect', 'Disconnect': 'Disconnect'},
    'ICapeUnitReport': {'GetSelectedReport': 'selectedReport',
                        'SetSelectedReport': 'selectedReport',
                        'GetReports': 'reports',
                        'ProduceReport': 'ProduceReport'},
    # The COM binding spells the getter 'Variable', the specification
    # 'GetIndex'. Both take (Variable_type, Component); the setter agrees.
    'ICapeUnitPortVariables': {'SetIndex': 'SetIndex', 'GetIndex': 'Variable'},
}

IUNKNOWN_IDISPATCH = {'QueryInterface', 'AddRef', 'Release', 'GetTypeInfoCount',
                      'GetTypeInfo', 'GetIDsOfNames', 'Invoke'}


def check_against_tlb():
    """Assert every emitted operation maps onto a member of the COM interface,
    and that no COM member is left uncovered. Returns a status string."""
    try:
        import pythoncom  # noqa: PLC0415
    except ImportError:
        return 'SKIPPED (pywin32 not available)'

    common = os.environ.get('CommonProgramFiles', r'C:\Program Files\Common Files')
    tlb_path = os.path.join(common, 'CAPE-OPEN', 'Type Libraries',
                            'CAPE-OPENv1-0-0.tlb')
    if not os.path.exists(tlb_path):
        return 'SKIPPED (%s not installed)' % tlb_path

    tlb = pythoncom.LoadTypeLib(tlb_path)
    members = {}
    for i in range(tlb.GetTypeInfoCount()):
        name = tlb.GetDocumentation(i)[0]
        ti = tlb.GetTypeInfo(i)
        attr = ti.GetTypeAttr()
        got = set()
        for k in range(attr.cFuncs):
            fd = ti.GetFuncDesc(k)
            fn = ti.GetNames(fd.memid)[0]
            if fn not in IUNKNOWN_IDISPATCH:
                got.add(fn)
        members[name] = got

    problems = []
    for _mod, iface, _base, ops in INTERFACES:
        if iface not in members:
            problems.append('%s: absent from the type library' % iface)
            continue
        mapping = TLB_NAME_MAP.get(iface, {})
        com = set(members[iface])
        covered = set()
        for o in ops:
            com_name = mapping.get(o['name'])
            if com_name is None:
                problems.append('%s.%s: no COM name mapped' % (iface, o['name']))
                continue
            if com_name not in com:
                problems.append('%s.%s -> COM %r: not in the type library (has %s)'
                                % (iface, o['name'], com_name, sorted(com)))
            covered.add(com_name)
        missed = com - covered
        if missed:
            problems.append('%s: COM members not covered by this IDL: %s'
                            % (iface, sorted(missed)))
    if problems:
        raise SystemExit('type-library cross-check FAILED:\n  ' +
                         '\n  '.join(problems))
    return 'OK (%d interfaces against CAPEOPEN100 in %s)' % (len(INTERFACES),
                                                             os.path.basename(tlb_path))


# ---------------------------------------------------------------------------
#  Emission
# ---------------------------------------------------------------------------

def emit_ops(ops, indent):
    out = []
    pad = ' ' * indent
    for o in ops:
        args = ', '.join('%s %s %s' % (d, t, n) for d, t, n in o['args'])
        out.append('%s// %s, p.%d' % (pad, o['name'], o['page']))
        out.append('%s%s %s(%s)' % (pad, o['ret'], o['name'], args))
        raises = ',\n'.join('%s       %s%s' % (pad, E, e) for e in o['errors'])
        out.append('%s    raises(%s);' % (pad, raises.lstrip()))
    return out


def emit(header_text):
    L = [header_text,
         '#ifndef CAPEOPEN100_Unit_idl',
         '#define CAPEOPEN100_Unit_idl',
         '',
         '// Common::Types, Common::Error and Common::Identification are defined by the',
         '// Minlp file. Modules reopen across files in IDL, so this one only adds.',
         '#include "CAPEOPEN100_Minlp.idl"',
         '',
         'module CAPEOPEN100 {',
         '',
         '  module Common {',
         '']

    # Collection
    L.append('    module Collection {')
    L.append('')
    L.append('      interface ICapeCollection : %s {' % ID)
    L += emit_ops(COLLECTION, 8)
    L.append('      };')
    L.append('')
    L.append('    }; // END Collection')
    L.append('')

    # Utilities
    L.append('    module Utilities {')
    L.append('')
    L.append('      interface ICapeUtilities {')
    L += emit_ops(UTILITIES, 8)
    L.append('      };')
    L.append('')
    L.append('    }; // END Utilities')
    L.append('')

    # Parameter
    L.append('    module Parameter {')
    L.append('')
    for name, values in PARAMETER_ENUMS:
        L.append('      enum %s { %s };' % (name, ', '.join(values)))
    L.append('')
    for mod, iface, base, ops in INTERFACES:
        if mod != 'Common::Parameter':
            continue
        L.append('      interface %s%s {' % (iface, (' : ' + base) if base else ''))
        L += emit_ops(ops, 8)
        L.append('      };')
        L.append('')
    L.append('    }; // END Parameter')
    L.append('')

    # ECapeBadCOParameter reopens Common::Error. It carries an ICapeParameter
    # reference (Error Common Interface.pdf 3.3.7), so it has to come after
    # Common::Parameter. Body = ECapeUser's six members (5.2.1 flattening,
    # same as every exception in the Minlp file) + its own two attributes.
    L.append('    module Error {')
    L.append('')
    L.append('      // Error Common Interface.pdf 3.3.7. Not emitted by the Minlp file --')
    L.append('      // no Minlp operation raises it; ICapeUnit::Calculate/Validate do.')
    L.append('      exception ECapeBadCOParameter {')
    L.append('        ::CAPEOPEN100::Common::Types::CapeLong   code;')
    L.append('        ::CAPEOPEN100::Common::Types::CapeString description;')
    L.append('        ::CAPEOPEN100::Common::Types::CapeString scope;')
    L.append('        ::CAPEOPEN100::Common::Types::CapeString interfaceName;')
    L.append('        ::CAPEOPEN100::Common::Types::CapeString operation;')
    L.append('        ::CAPEOPEN100::Common::Types::CapeURL    moreInfo;')
    L.append('        ::CAPEOPEN100::Common::Types::CapeString parameterName;')
    L.append('        ::CAPEOPEN100::Common::Parameter::ICapeParameter parameter;')
    L.append('      };')
    L.append('')
    L.append('    }; // END Error')
    L.append('')
    L.append('  }; // END Common')
    L.append('')

    # Business::UnitOp::Unit
    L.append('  module Business {')
    L.append('    module UnitOp {')
    L.append('      module Unit {')
    L.append('')
    for name, values in UNIT_ENUMS:
        L.append('        enum %s { %s };' % (name, ', '.join(values)))
    L.append('')
    for mod, iface, base, ops in INTERFACES:
        if mod != 'Business::UnitOp::Unit':
            continue
        L.append('        interface %s%s {' % (iface, (' : ' + base) if base else ''))
        L += emit_ops(ops, 10)
        L.append('        };')
        L.append('')
    L.append('      }; // END Unit')
    L.append('    }; // END UnitOp')
    L.append('  }; // END Business')
    L.append('')
    L.append('}; // END CAPEOPEN100')
    L.append('')
    L.append('#endif // CAPEOPEN100_Unit_idl')
    return '\n'.join(L) + '\n'


HEADER = '''// ============================================================================
//  CAPEOPEN100_Unit.idl
//  --------------------------------------------------------------------------
//  This file is a part of project xRtoCapeOpen.
//
//  GENERATED by tools/gen_capeopen_unit_idl.py -- do not edit by hand.
//  Re-run that script to regenerate and diff.
// ============================================================================
//  A reconstruction of the Unit Operation / Collection / Parameter subset of
//  the CAPE-OPEN v1.0 CORBA IDL, companion to CAPEOPEN100_Minlp.idl.
//
//  WHY THIS FILE EXISTS
//    ICapeMINLP carries an optimisation problem and nothing else -- no ports,
//    no component slate. Those live in Unit Operations (ICapeUnit,
//    ICapeUnitPort) and are what a flowsheet needs in order to wire a remote
//    model into a flowsheet at all.
//
//    The official CAPE-OPENv1-0-0.idl is still not obtainable: CO-LaN's
//    "CAPE-OPEN IDL" download area carries only the two documentation-set zips,
//    the COM TLB/PIA installers and a TLB guide, and the source browser needs
//    a login. So this is reconstructed the same way the Minlp file was.
//
//    Beware the decoy in docs/: 07_CO_Sequential_Modular_Specific_Tools.zip
//    contains three real .idl files, but box/idl/notes.txt calls them
//    "proto v5 ... non normalises CO", and they show it -- no modules at all,
//    CapeDoubleSequence for CapeArrayDouble, CapeUNKNOWN for ECapeUnknown.
//    They are a pre-standard prototype and are NOT a source for this file.
//
//  PROVENANCE, DEVIATIONS AND THE TYPE-LIBRARY CROSS-CHECK
//    All recorded in the generator's header comment. Read it before trusting
//    any single line here -- in particular deviation (a): the raises clauses
//    follow the specification's "Errors" rows, and those rows are not
//    exhaustive.
//
//  STATUS: self-interop only. Both ends of every link tested so far were
//  built from this same file, so what is verified is that the reconstruction
//  is internally consistent and speaks standard GIOP -- NOT that it matches
//  the official IDL. Do not claim compliance to a third party on its basis.
// ============================================================================
'''


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = (sys.argv[2] if len(sys.argv) > 2
                else os.path.join(root, 'CAPEOPEN100_Unit.idl'))

    status = check_against_tlb()
    text = emit(HEADER)

    non_ascii = [(i + 1, l) for i, l in enumerate(text.splitlines())
                 if any(ord(c) > 127 for c in l)]
    if non_ascii:
        raise SystemExit('refusing to emit: non-ASCII on lines %s'
                         % [n for n, _ in non_ascii])

    with open(out_path, 'w', encoding='ascii', newline='\n') as f:
        f.write(text)
    n_ops = sum(len(ops) for _m, _i, _b, ops in INTERFACES)
    print('type-library cross-check: %s' % status)
    print('wrote %s (%d interfaces, %d operations)'
          % (out_path, len(INTERFACES), n_ops))


if __name__ == '__main__':
    main()
