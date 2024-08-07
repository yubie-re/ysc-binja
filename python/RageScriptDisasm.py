from io import BytesIO
import struct
from binaryninja.function import InstructionTextTokenType, InstructionTextToken
from binaryninja.architecture import InstructionInfo, BranchType
from RageUtil import *

INSTRUCT_NAMES = [
    'NOP',
    'IADD',
    'ISUB',
    'IMUL',
    'IDIV',
    'IMOD',
    'INOT',
    'INEG',
    'IEQ',
    'INE',
    'IGT',
    'IGE',
    'ILT',
    'ILE',
    'FADD',
    'FSUB',
    'FMUL',
    'FDIV',
    'FMOD',
    'FNEG',
    'FEQ',
    'FNE',
    'FGT',
    'FGE',
    'FLT',
    'FLE',
    'VADD',
    'VSUB',
    'VMUL',
    'VDIV',
    'VNEG',
    'IAND',
    'IOR',
    'IXOR',
    'I2F',
    'F2I',
    'F2V',
    'PUSH_CONST_U8',
    'PUSH_CONST_U8_U8',
    'PUSH_CONST_U8_U8_U8',
    'PUSH_CONST_U32',
    'PUSH_CONST_F',
    'DUP',
    'DROP',
    'NATIVE',
    'ENTER',
    'LEAVE',
    'LOAD',
    'STORE',
    'STORE_REV',
    'LOAD_N',
    'STORE_N',
    'ARRAY_U8',
    'ARRAY_U8_LOAD',
    'ARRAY_U8_STORE',
    'LOCAL_U8',
    'LOCAL_U8_LOAD',
    'LOCAL_U8_STORE',
    'STATIC_U8',
    'STATIC_U8_LOAD',
    'STATIC_U8_STORE',
    'IADD_U8',
    'IMUL_U8',
    'IOFFSET',
    'IOFFSET_U8',
    'IOFFSET_U8_LOAD',
    'IOFFSET_U8_STORE',
    'PUSH_CONST_S16',
    'IADD_S16',
    'IMUL_S16',
    'IOFFSET_S16',
    'IOFFSET_S16_LOAD',
    'IOFFSET_S16_STORE',
    'ARRAY_U16',
    'ARRAY_U16_LOAD',
    'ARRAY_U16_STORE',
    'LOCAL_U16',
    'LOCAL_U16_LOAD',
    'LOCAL_U16_STORE',
    'STATIC_U16',
    'STATIC_U16_LOAD',
    'STATIC_U16_STORE',
    'GLOBAL_U16',
    'GLOBAL_U16_LOAD',
    'GLOBAL_U16_STORE',
    'J',
    'JZ',
    'IEQ_JZ',
    'INE_JZ',
    'IGT_JZ',
    'IGE_JZ',
    'ILT_JZ',
    'ILE_JZ',
    'CALL',
    'STATIC_U24',
    'STATIC_U24_LOAD',
    'STATIC_U24_STORE',
    'GLOBAL_U24',
    'GLOBAL_U24_LOAD',
    'GLOBAL_U24_STORE',
    'PUSH_CONST_U24',
    'SWITCH',
    'STRING',
    'STRINGHASH',
    'TEXT_LABEL_ASSIGN_STRING',
    'TEXT_LABEL_ASSIGN_INT',
    'TEXT_LABEL_APPEND_STRING',
    'TEXT_LABEL_APPEND_INT',
    'TEXT_LABEL_COPY',
    'CATCH',
    'THROW',
    'CALLINDIRECT',
    'PUSH_CONST_M1',
    'PUSH_CONST_0',
    'PUSH_CONST_1',
    'PUSH_CONST_2',
    'PUSH_CONST_3',
    'PUSH_CONST_4',
    'PUSH_CONST_5',
    'PUSH_CONST_6',
    'PUSH_CONST_7',
    'PUSH_CONST_FM1',
    'PUSH_CONST_F0',
    'PUSH_CONST_F1',
    'PUSH_CONST_F2',
    'PUSH_CONST_F3',
    'PUSH_CONST_F4',
    'PUSH_CONST_F5',
    'PUSH_CONST_F6',
    'PUSH_CONST_F7',
    'IS_BIT_SET'
]




ONE_BYTE_OPERAND_INSTRUCTIONS = (OP_PUSH_CONST_U8, OP_ARRAY_U8, OP_ARRAY_U8_LOAD, OP_ARRAY_U8_STORE, OP_LOCAL_U8_LOAD, OP_LOCAL_U8_STORE,
                                 OP_STATIC_U8, OP_STATIC_U8_LOAD, OP_STATIC_U8_STORE, OP_IADD_U8, OP_IMUL_U8, OP_IOFFSET_U8,
                                 OP_IOFFSET_U8_LOAD, OP_IOFFSET_U8_STORE, OP_TEXT_LABEL_ASSIGN_STRING, OP_TEXT_LABEL_ASSIGN_INT,
                                 OP_TEXT_LABEL_APPEND_STRING, OP_TEXT_LABEL_APPEND_INT)

TWO_BYTE_OPERAND_INSTRUCTIONS = (OP_PUSH_CONST_S16, OP_IADD_S16, OP_IMUL_S16, OP_IOFFSET_S16, OP_IOFFSET_S16_LOAD, OP_IOFFSET_S16_STORE,
                                 OP_ARRAY_U16, OP_ARRAY_U16_LOAD, OP_ARRAY_U16_STORE, OP_LOCAL_U16, OP_LOCAL_U16_LOAD, OP_LOCAL_U16_STORE, OP_STATIC_U16,
                                 OP_STATIC_U16_LOAD, OP_STATIC_U16_STORE, OP_GLOBAL_U16, OP_GLOBAL_U16_LOAD, OP_GLOBAL_U16_STORE)

THREE_BYTE_OPERAND_INSTRUCTIONS = (OP_STATIC_U24, OP_STATIC_U24_LOAD, OP_STATIC_U24_STORE,
                                   OP_GLOBAL_U24, OP_GLOBAL_U24_LOAD, OP_GLOBAL_U24_STORE, OP_PUSH_CONST_U24)

JUMP_INSTRUCTIONS = [OP_J, OP_JZ, OP_IEQ_JZ, OP_INE_JZ,
                     OP_IGT_JZ, OP_IGE_JZ, OP_ILT_JZ, OP_ILE_JZ]


def dis_insn(stream: BytesIO, address: int):
    start = stream.tell()
    insn_type = get_next_byte(stream)
    if (insn_type > OP_IS_BIT_SET):
        return [InstructionTextToken(InstructionTextTokenType.InstructionToken, 'UNK')], 1
    name_token = InstructionTextToken(
        InstructionTextTokenType.InstructionToken, INSTRUCT_NAMES[insn_type])
    tokens = [name_token]
    tokens.append(InstructionTextToken(
        InstructionTextTokenType.OperandSeparatorToken, ' '))

    if insn_type in ONE_BYTE_OPERAND_INSTRUCTIONS:
        operand_one = get_next_byte(stream)
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_one), operand_one))
    elif insn_type in TWO_BYTE_OPERAND_INSTRUCTIONS:
        operand_one = get_next_word(stream)
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_one), operand_one))
    elif insn_type in THREE_BYTE_OPERAND_INSTRUCTIONS:
        operand_one = get_next_u24(stream)
    elif insn_type in JUMP_INSTRUCTIONS:
        operand_one = address + get_next_word(stream) + 3
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.PossibleAddressToken, format_address(operand_one), operand_one))
    elif insn_type == OP_PUSH_CONST_U8_U8:
        operand_one = get_next_byte(stream)
        operand_two = get_next_byte(stream)
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_one), operand_one))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.OperandSeparatorToken, ', '))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_two), operand_two))
    elif insn_type == OP_PUSH_CONST_U8_U8_U8:
        operand_one = get_next_byte(stream)
        operand_two = get_next_byte(stream)
        operand_three = get_next_byte(stream)
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_one), operand_one))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.OperandSeparatorToken, ', '))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_two), operand_two))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.OperandSeparatorToken, ', '))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_three), operand_three))
    elif insn_type == OP_PUSH_CONST_U32:
        operand_one = get_next_dword(stream)
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_one), operand_one))
    elif insn_type == OP_PUSH_CONST_F:
        operand_one = get_next_float(stream)
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, str(operand_one)))
    elif insn_type == OP_NATIVE:
        operand_one = get_next_byte(stream)
        operand_two = (operand_one >> 2) & 63
        operand_one &= 3
        operand_three = ((get_next_byte(stream) << 8) | get_next_byte(stream)) * 8
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_one), operand_one))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.OperandSeparatorToken, ', '))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_two), operand_two))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.OperandSeparatorToken, ', '))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_three), operand_three))
    elif insn_type == OP_ENTER:
        operand_one = get_next_byte(stream)
        operand_two = get_next_word(stream)
        operand_three = get_next_byte(stream)
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_one), operand_one))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.OperandSeparatorToken, ', '))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_two), operand_two))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.OperandSeparatorToken, ', '))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_three), operand_three))
    elif insn_type == OP_LEAVE:
        operand_one = get_next_byte(stream)
        operand_two = get_next_byte(stream)
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_one), operand_one))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.OperandSeparatorToken, ', '))
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_two), operand_two))
    elif insn_type == OP_CALL:
        operand_one = get_next_u24(stream)
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.PossibleAddressToken, format_address(operand_one), operand_one))
    elif insn_type == OP_SWITCH:
        operand_one = get_next_byte(stream)
        size = 2 + operand_one * 6
        if size > 256:
            size = 256
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, str(operand_one), operand_one))
        return tokens, 2
    elif insn_type == OP_STRING:
        pass
    elif insn_type == OP_PUSH_CONST_M1:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, '-1', -1))
    elif insn_type == OP_PUSH_CONST_0:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, '0', 0))
    elif insn_type == OP_PUSH_CONST_1:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, '1', 1))
    elif insn_type == OP_PUSH_CONST_2:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, '2', 2))
    elif insn_type == OP_PUSH_CONST_3:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, '3', 3))
    elif insn_type == OP_PUSH_CONST_4:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, '4', 4))
    elif insn_type == OP_PUSH_CONST_5:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, '5', 5))
    elif insn_type == OP_PUSH_CONST_6:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, '6', 7))
    elif insn_type == OP_PUSH_CONST_7:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.IntegerToken, '7', 7))
    elif insn_type == OP_PUSH_CONST_FM1:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, '-1.0', -1))
    elif insn_type == OP_PUSH_CONST_F0:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, '0.0', 0))
    elif insn_type == OP_PUSH_CONST_F1:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, '1.0', 1))
    elif insn_type == OP_PUSH_CONST_F2:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, '2.0', 2))
    elif insn_type == OP_PUSH_CONST_F3:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, '3.0', 3))
    elif insn_type == OP_PUSH_CONST_F4:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, '4.0', 4))
    elif insn_type == OP_PUSH_CONST_F5:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, '5.0', 5))
    elif insn_type == OP_PUSH_CONST_F6:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, '6.0', 6))
    elif insn_type == OP_PUSH_CONST_F7:
        tokens.append(InstructionTextToken(
            InstructionTextTokenType.FloatingPointToken, '7.0', 7))
    return tokens, stream.tell() - start


def get_branch_info(stream, address, info: InstructionInfo):
    insn_type = get_next_byte(stream)
    if insn_type == OP_J:
        operand_one = address + get_next_word(stream) + 3
        info.add_branch(BranchType.UnconditionalBranch, operand_one)
    elif insn_type == OP_JZ:
        operand_one = address + get_next_word(stream) + 3
        info.add_branch(BranchType.FalseBranch, operand_one)
        info.add_branch(BranchType.TrueBranch, address + 3)
    elif insn_type == OP_IEQ_JZ:
        operand_one = address + get_next_word(stream) + 3
        info.add_branch(BranchType.FalseBranch, operand_one)
        info.add_branch(BranchType.TrueBranch, address + 3)
    elif insn_type == OP_INE_JZ:
        operand_one = address + get_next_word(stream) + 3
        info.add_branch(BranchType.FalseBranch, operand_one)
        info.add_branch(BranchType.TrueBranch, address + 3)
    elif insn_type == OP_IGT_JZ:
        operand_one = address + get_next_word(stream) + 3
        info.add_branch(BranchType.FalseBranch, operand_one)
        info.add_branch(BranchType.TrueBranch, address + 3)
    elif insn_type == OP_IGE_JZ:
        operand_one = address + get_next_word(stream)
        info.add_branch(BranchType.FalseBranch, operand_one)
        info.add_branch(BranchType.TrueBranch, address + 3)
    elif insn_type == OP_ILT_JZ:
        operand_one = address + get_next_word(stream) + 3
        info.add_branch(BranchType.FalseBranch, operand_one)
        info.add_branch(BranchType.TrueBranch, address + 3)
    elif insn_type == OP_ILE_JZ:
        operand_one = address + get_next_word(stream) + 3
        info.add_branch(BranchType.FalseBranch, operand_one)
        info.add_branch(BranchType.TrueBranch, address + 3)
    elif insn_type == OP_CALL:
        operand_one = get_next_u24(stream)
        info.add_branch(BranchType.CallDestination, operand_one)
    elif insn_type == OP_LEAVE:
        info.add_branch(BranchType.FunctionReturn, address)
    elif insn_type == OP_SWITCH:
        operand_one = get_next_byte(stream)
        info.add_branch(BranchType.UnresolvedBranch, operand_one)
    elif insn_type == OP_NATIVE:
        info.add_branch(BranchType.SystemCall, 0)


if __name__ == '__main__':
    byte = bytearray.fromhex("2D00020000723C0225863C0325863C04723C05723C06723C0725863C08723C09250C3C0A250C3C0B296F12833A3C0C703C0F71663C10290000A0423C152900000C433C1629000034433C177A3C1A299A9919BD3C1E297B142E3E3C1F743C22723C2625413C2725313C2825403C2929CDCC4C3D29CDCC8C3E0E290AD7233C0F3C3D703CA37D3CA57A3CA67C3CA7290000C8423CA8290000003F3CB1723CB7723CDD25223CDE3BDE3CDF3BDE3CE03BDE3CE13BDE3CE23BDE3CE33BDE3CE43BDE3CE53BDE3CE63BDE3CE73BDE3CE83BDE3CE93BDE3CEA3BDE3CEB3BDE3CEC3BDE3CED3BDE3CEE3BDE3CEF3BDE3CF03BDE3CF13BDE3CF23BDE3CF33BDE3CF43BDE3CF53BDE3CF63BDE3CF73BDE3CF8251A3CF925823CFA25723CFB250A3CFC251A3CFD250D3CFE250D3CFF25145100012533510101253351020125335103012533510401253351050172510601725108017351090174510A0175510B0172510D0173510E0174510F017551100176511101775112017851130125085114012509511501250A511601250B511701250C511801250D511901250E511A01250F511B012510511C012511511D017251200173512101745122017551230176512401775125017851260125085127012509512801250A512901250B512A01250C512B01250D512C01250E512D0170512E0170512F017051300170513101705132017051330170513401705135017051360170513701705138017051390170513A0170513B0125A3513C0172513D0176514C017251560173515701745158017551590176515A0177515B0178515C012508515D0172515E0172515F017251600172516101745162017351630170516F05725173056420BF0251CE056101000447ED3251CF056101000447EE3251D005282ECFBB4751DF052847F8C6ED51E00528AF82F65751E10528EA545E1E51E20528F116009251E3052873FA826851E405280F1EF29C51E50528512EF48451E605705172067051730673518406439DFF5188067051930672519D067051AB067051E506439DFF51E7067051F006250F51750770518607705196077A296666663F7A744F9907336141F92846050C41AB514F0C7051500C7251D00C7251D10C7051D50C5D3CCC2E5D34CC2E4F530C61010004646480003F2F61010004646180003F2F5DF2CB2E72565B005D408D2E5D338D2E5D4A8C2E5604005D2E8C2E4F530C5D708B2E4F530C5D758A2E4F530C5DFB892E5DF0892E6503000000000F000100000014000400000015005515005DD2642E72518F07550A005DAF030055030055000055A1FF2E00002D000200005D89642E2B5D24122E5D8A0F2E619BC9294672144754015604005DC1062E50EF056503000000000F0001000000D60002000000BC0255D0026165C82846B7034106715A90007666712C08039276662C0503EF5675005D81062E566E002C0103A572725D21062E565D002C0003AB6165C82846B70341055173065073065174065074065D0A062E250D4F7506335D94052E5D5F022E2C0103A55D16022E51D6055DC1062E5D72002E5D09FE2D5DDDF32D72518E067061C05A294806207251EF055DE1F22D5504007351EF057151CB057171722C0C04E150950625128256140061448C0147CA05065609004F950625122C0800404F9606762C0800405D87F22D5D7DF02D5503025095062512820656A1005D49F02D732C0503C45610002C01036E065608007325C8722C0C04E15DABEE2D71725D0FEE2D2A5607005091067282061F566A0050D6055618002C0103A55D16022E06560C005D94052E5D5F022E7151D605509606250F82062A560800509606251482061F561700717171725D92EA2D5D50E42D5DC1C42A5D7F262855090026B11366705DD025283BE03BDE5B0B005DBF25285604007251D6055D1D2128551B004FBC075D04212861448C0147CA05065609004F950625122C0800405D651F282C00026962625A292524082A0656080062625A29251D08202A0656080062625A29251B08202A0656080062625A29253608202A0656080062625A29253C08202A0656080062625A29254908202A0656080062625A2925740820560C0029CDCCCC3E2C0402825504002C0003AB5DDEFE2756660050E906065657007351EF052C0103A55DCBFD272A56050062A207401F563C002C0103A5610F281C496D03460B0141207282062A5614002C0103A5610F281C496D03460B0141207482061F2A06560500622F2329205604005D29FC275508007151E9065D11810D509506251282065637005096067682560B005D147E035D6379035524005096067782561C004F90072564715DC07803560F004F9606772C0800404F90075DB37803715DE776035517005DC658032C0003AB5D5E58035DBF06007151EF055500002E00002D000500002514390328F57564893904726165C82846B703420E716165C82846B7034210509506251482560C007239024F950625142C0800402C0103A5610F281C496D03460B0141202510825652002C0103A5610F281C496D03460B0141207282062A5614002C0103A5610F281C496D03460B0141207482061F2A06560500622F2329205604005D29FC272C0103A5610F281C496D03460B01402025102C0800405D4158035604005D0658035DE757035604005DA857032C0103A572725D21062E56000050950675825647005D72B902560E0072390225093903288D03A44639042C01017F2C04049E5D32B902283EEC697A72725D33B8025DECB702285B586D542899E551BF715DEA75024F9506752C0800405D512E006125071C4103788263EDA41D509606251082560C004F960625102C080040723902509606251582560C004F960625152C080040723902715DF42D005DB322005098067782561C004F9806772C080040380206560E00723902250A390328F5756489390438027257090038043803715DCB21005096062508825616005D6F13005D3413004F960625082C0800407263E95545706165C82846B7034206706165C82846B7034205715D1D13005D1212004F9506251F2C0800404F960625092C0800404FBC075D042128722C04009E72705D8610005D6B0F005D512E00712C0401BA716165C82846B7034204726165C82846B703420C4FBE0743E7035DDD0800251D662C0404B72E00002D0204000038012564574300726180012048700C38004001402A732C08004038004001402A712C08004038004001402A722C080040716180012048100B380040015DA10E003800712C080040557C003800722C080040726180012048700C38004001412A72822A0656090038004001412A71822056470071380046B60135012C050420560C0071380046B60134012C0403E072380046B60135012C05042056140072380046B60135012528662C0900132B2C00032B380046B9015DA80D0038004001725DAB0900713800302E02002D02040000380041047158100038004004380040053800402C5D7C0D00713800350171092A0656080072380035017109202A0656080073380035017109202A06560500380172082056140038005DA10E0038005DF40C00616B12205D150C006180012047940A71092A065608006180012047700C205604005D540A002532662C0503DB5607002532662C0404902C010034560A00611C172040315DB3780371619BC92948E50F2E02002D004F000037042A403C77312B2A404377312B2B7139023802745B3D007139033803250C5B1E00254B370432254B3803380261800120498503344B3338033D01390355DBFF7138026180012046900A360138023D01390255BDFF716180012048940A716180012048950A7139027139023802775B3D007138026180012046960A3601254466380261800120469D0A340668187138026180012046C20A36017138026180012046C90A360138023D01390255BDFF716180012048D00A716180012048D10A716180012048D20A7139027139023802745B21007138026180012046D30A36017138026180012046D70A360138023D01390255D9FF716180012048DB0A6180012046DC0A5DBF0B006180012046070B5DB37803706180012048090B7161800120480A0B61800120460B0B5DB378037161800120480D0B61800120460E0B5DB37803716180012048100B716180012046DC0A421C716180012046DC0A421B716180012048700C2E00002D01040000713800302544663800400168202544663800400968407238004219713800421A713800421B713800421C7139033803775B1B007138033800401D360171380338004024360138033D01390355DFFF2E01002D011200007139103810250C5BC200254466381038003464684025446638103800346440106840250D370332250D381038003464402033250D370332250D381038003464402D3371381038003464423A71381038003464423B7139113811775B23007A3811381038003464403C36017138113810380034644043360138113D01391155D7FF71381038003464424B71381038003464424A71381038003464424C71381038003464424D71381038003464424E71381038003464424F2544663810380034644050681038103D0139105537FF61800120460E0B5DB378032E01002D0111000071380042F670380040F6420171380040F64202380040F640035DB3780371380040F642057139033803250C5B3500703803380040F64006360F713803380040F64006340F4201250D370432250D3803380040F64006340F40023338033D01390355C4FF7139033803745B1400713803380040F640BB360138033D01390355E6FF2E01002D03050000713800307138013071630000206100002040015DB3780338022F38024101702C0D01802B2E03002D0103000038005DBC0D0072380048B4022E01002D01040000713800307139037139033803250C5B7E0025446638033800400134396840254466380338004001343940106840254466380338004001343940206810713803380040013439422471380338004001343942252544663803380040013439402668407338033800400134394236436D0138033800400134394237436D013803380040013439423838033D013903557BFF71380048AE0271380048AF0271380048B00271380048B10271380048B30271380048B20271380048B40271380048B5027A380048B7027A380048B8027A380048B9027A380048BA027B380048BB022E0100")
    f = BytesIO(byte)

    size = len(f.getvalue())
    cursor = 0
    print(size)

    # Reset file cursor to the beginning
    f.seek(0)

    # Read and disassemble instructions until the end of the file
    while cursor < size:
        if not f.readable():
            break

        tokens, insn_size = dis_insn(f, 1)
        print(insn_size, tokens)

        cursor += insn_size
        print(cursor)
