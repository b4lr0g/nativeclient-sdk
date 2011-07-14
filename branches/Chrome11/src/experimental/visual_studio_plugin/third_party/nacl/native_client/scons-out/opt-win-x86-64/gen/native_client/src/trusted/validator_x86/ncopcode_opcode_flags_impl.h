/* \gen\native_client\src\trusted\validator_x86\ncopcode_opcode_flags_impl.h
 * THIS FILE IS AUTO_GENERATED DO NOT EDIT.
 *
 * This file was auto-generated by enum_gen.py
 * from file ncopcode_opcode_flags.enum
 */

/* Define the corresponding names of NaClIFlag. */
static const char* const g_NaClIFlagName[NaClIFlagEnumSize + 1] = {
  "OpcodeUsesRexW",
  "OpcodeHasRexR",
  "OpcodeInModRm",
  "OpcodeLtC0InModRm",
  "ModRmModIs0x3",
  "OpcodeUsesModRm",
  "OpcodeHasImmed",
  "OpcodeHasImmed_b",
  "OpcodeHasImmed_w",
  "OpcodeHasImmed_v",
  "OpcodeHasImmed_p",
  "OpcodeHasImmed_o",
  "OpcodeHasImmed2_b",
  "OpcodeHasImmed2_w",
  "OpcodeHasImmed2_v",
  "OpcodeHasImmed_Addr",
  "OpcodePlusR",
  "OpcodePlusI",
  "OpcodeRex",
  "OpcodeLegacy",
  "OpcodeLockable",
  "Opcode32Only",
  "Opcode64Only",
  "OperandSize_b",
  "OperandSize_w",
  "OperandSize_v",
  "OperandSize_o",
  "AddressSize_w",
  "AddressSize_v",
  "AddressSize_o",
  "NaClIllegal",
  "OperandSizeDefaultIs64",
  "OperandSizeForce64",
  "AddressSizeDefaultIs32",
  "IgnorePrefixDATA16",
  "IgnorePrefixSEGCS",
  "NaClIFlagEnumSize"
};

const char* NaClIFlagName(NaClIFlag name) {
  return name <= NaClIFlagEnumSize
    ? g_NaClIFlagName[name]
    : "NaClIFlag???";
}