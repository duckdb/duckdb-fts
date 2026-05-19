/* Generated from lovins.sbl by Snowball 3.0.0 - https://snowballstem.org/ */

#include "stem_UTF_8_lovins.h"

#include <stddef.h>

#include "../runtime/snowball_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif
extern int lovins_UTF_8_stem(struct SN_env * z);
#ifdef __cplusplus
}
#endif

static int r_respell(struct SN_env * z);
static int r_undouble(struct SN_env * z);
static int r_endings(struct SN_env * z);
static int r_CC(struct SN_env * z);
static int r_BB(struct SN_env * z);
static int r_AA(struct SN_env * z);
static int r_Z(struct SN_env * z);
static int r_Y(struct SN_env * z);
static int r_X(struct SN_env * z);
static int r_W(struct SN_env * z);
static int r_V(struct SN_env * z);
static int r_U(struct SN_env * z);
static int r_T(struct SN_env * z);
static int r_S(struct SN_env * z);
static int r_R(struct SN_env * z);
static int r_Q(struct SN_env * z);
static int r_P(struct SN_env * z);
static int r_O(struct SN_env * z);
static int r_N(struct SN_env * z);
static int r_M(struct SN_env * z);
static int r_L(struct SN_env * z);
static int r_K(struct SN_env * z);
static int r_J(struct SN_env * z);
static int r_I(struct SN_env * z);
static int r_H(struct SN_env * z);
static int r_G(struct SN_env * z);
static int r_F(struct SN_env * z);
static int r_E(struct SN_env * z);
static int r_D(struct SN_env * z);
static int r_C(struct SN_env * z);
static int r_B(struct SN_env * z);
static int r_A(struct SN_env * z);

static const symbol s_0[] = { 'l', 'l' };
static const symbol s_1[] = { 'd', 'r' };
static const symbol s_2[] = { 'i', 'n' };
static const symbol s_3[] = { 'm', 'e', 't' };
static const symbol s_4[] = { 'r', 'y', 's', 't' };
static const symbol s_5[] = { 'i', 'e', 'f' };
static const symbol s_6[] = { 'u', 'c' };
static const symbol s_7[] = { 'u', 'm' };
static const symbol s_8[] = { 'r', 'b' };
static const symbol s_9[] = { 'u', 'r' };
static const symbol s_10[] = { 'i', 's', 't', 'e', 'r' };
static const symbol s_11[] = { 'm', 'e', 't', 'e', 'r' };
static const symbol s_12[] = { 'o', 'l', 'u', 't' };
static const symbol s_13[] = { 'l' };
static const symbol s_14[] = { 'b', 'i', 'c' };
static const symbol s_15[] = { 'd', 'i', 'c' };
static const symbol s_16[] = { 'p', 'i', 'c' };
static const symbol s_17[] = { 't', 'i', 'c' };
static const symbol s_18[] = { 'a', 'c' };
static const symbol s_19[] = { 'e', 'c' };
static const symbol s_20[] = { 'i', 'c' };
static const symbol s_21[] = { 'l', 'u', 'c' };
static const symbol s_22[] = { 'u', 'a', 's' };
static const symbol s_23[] = { 'v', 'a', 's' };
static const symbol s_24[] = { 'c', 'i', 's' };
static const symbol s_25[] = { 'l', 'i', 's' };
static const symbol s_26[] = { 'e', 'r', 'i', 's' };
static const symbol s_27[] = { 'p', 'a', 'n', 's' };
static const symbol s_28[] = { 'e', 'n', 's' };
static const symbol s_29[] = { 'o', 'n', 's' };
static const symbol s_30[] = { 'l', 'u', 's' };
static const symbol s_31[] = { 'r', 'u', 's' };
static const symbol s_32[] = { 'h', 'e', 's' };
static const symbol s_33[] = { 'm', 'i', 's' };
static const symbol s_34[] = { 'e', 'n', 's' };
static const symbol s_35[] = { 'e', 'r', 's' };
static const symbol s_36[] = { 'e', 's' };
static const symbol s_37[] = { 'y', 's' };

static const symbol s_0_0[1] = { 'd' };
static const symbol s_0_1[1] = { 'f' };
static const symbol s_0_2[2] = { 'p', 'h' };
static const symbol s_0_3[2] = { 't', 'h' };
static const symbol s_0_4[1] = { 'l' };
static const symbol s_0_5[2] = { 'e', 'r' };
static const symbol s_0_6[2] = { 'o', 'r' };
static const symbol s_0_7[2] = { 'e', 's' };
static const symbol s_0_8[1] = { 't' };
static const struct among a_0[9] = {
{ 1, s_0_0, 0, -1, 0},
{ 1, s_0_1, 0, -1, 0},
{ 2, s_0_2, 0, -1, 0},
{ 2, s_0_3, 0, -1, 0},
{ 1, s_0_4, 0, -1, 0},
{ 2, s_0_5, 0, -1, 0},
{ 2, s_0_6, 0, -1, 0},
{ 2, s_0_7, 0, -1, 0},
{ 1, s_0_8, 0, -1, 0}
};

static const symbol s_1_0[2] = { 's', '\'' };
static const symbol s_1_1[1] = { 'a' };
static const symbol s_1_2[2] = { 'i', 'a' };
static const symbol s_1_3[3] = { 'a', 't', 'a' };
static const symbol s_1_4[2] = { 'i', 'c' };
static const symbol s_1_5[3] = { 'a', 'i', 'c' };
static const symbol s_1_6[5] = { 'a', 'l', 'l', 'i', 'c' };
static const symbol s_1_7[4] = { 'a', 'r', 'i', 'c' };
static const symbol s_1_8[4] = { 'a', 't', 'i', 'c' };
static const symbol s_1_9[4] = { 'i', 't', 'i', 'c' };
static const symbol s_1_10[5] = { 'a', 'n', 't', 'i', 'c' };
static const symbol s_1_11[5] = { 'i', 's', 't', 'i', 'c' };
static const symbol s_1_12[7] = { 'a', 'l', 'i', 's', 't', 'i', 'c' };
static const symbol s_1_13[7] = { 'a', 'r', 'i', 's', 't', 'i', 'c' };
static const symbol s_1_14[7] = { 'i', 'v', 'i', 's', 't', 'i', 'c' };
static const symbol s_1_15[2] = { 'e', 'd' };
static const symbol s_1_16[5] = { 'a', 'n', 'c', 'e', 'd' };
static const symbol s_1_17[5] = { 'e', 'n', 'c', 'e', 'd' };
static const symbol s_1_18[5] = { 'i', 's', 'h', 'e', 'd' };
static const symbol s_1_19[3] = { 'i', 'e', 'd' };
static const symbol s_1_20[4] = { 'e', 'n', 'e', 'd' };
static const symbol s_1_21[5] = { 'i', 'o', 'n', 'e', 'd' };
static const symbol s_1_22[4] = { 'a', 't', 'e', 'd' };
static const symbol s_1_23[5] = { 'e', 'n', 't', 'e', 'd' };
static const symbol s_1_24[4] = { 'i', 'z', 'e', 'd' };
static const symbol s_1_25[6] = { 'a', 'r', 'i', 'z', 'e', 'd' };
static const symbol s_1_26[3] = { 'o', 'i', 'd' };
static const symbol s_1_27[5] = { 'a', 'r', 'o', 'i', 'd' };
static const symbol s_1_28[4] = { 'h', 'o', 'o', 'd' };
static const symbol s_1_29[5] = { 'e', 'h', 'o', 'o', 'd' };
static const symbol s_1_30[5] = { 'i', 'h', 'o', 'o', 'd' };
static const symbol s_1_31[7] = { 'e', 'l', 'i', 'h', 'o', 'o', 'd' };
static const symbol s_1_32[4] = { 'w', 'a', 'r', 'd' };
static const symbol s_1_33[1] = { 'e' };
static const symbol s_1_34[2] = { 'a', 'e' };
static const symbol s_1_35[4] = { 'a', 'n', 'c', 'e' };
static const symbol s_1_36[6] = { 'i', 'c', 'a', 'n', 'c', 'e' };
static const symbol s_1_37[4] = { 'e', 'n', 'c', 'e' };
static const symbol s_1_38[3] = { 'i', 'd', 'e' };
static const symbol s_1_39[5] = { 'i', 'c', 'i', 'd', 'e' };
static const symbol s_1_40[5] = { 'o', 't', 'i', 'd', 'e' };
static const symbol s_1_41[3] = { 'a', 'g', 'e' };
static const symbol s_1_42[4] = { 'a', 'b', 'l', 'e' };
static const symbol s_1_43[6] = { 'a', 't', 'a', 'b', 'l', 'e' };
static const symbol s_1_44[6] = { 'i', 'z', 'a', 'b', 'l', 'e' };
static const symbol s_1_45[8] = { 'a', 'r', 'i', 'z', 'a', 'b', 'l', 'e' };
static const symbol s_1_46[4] = { 'i', 'b', 'l', 'e' };
static const symbol s_1_47[7] = { 'e', 'n', 'c', 'i', 'b', 'l', 'e' };
static const symbol s_1_48[3] = { 'e', 'n', 'e' };
static const symbol s_1_49[3] = { 'i', 'n', 'e' };
static const symbol s_1_50[5] = { 'i', 'd', 'i', 'n', 'e' };
static const symbol s_1_51[3] = { 'o', 'n', 'e' };
static const symbol s_1_52[5] = { 'a', 't', 'u', 'r', 'e' };
static const symbol s_1_53[6] = { 'e', 'a', 't', 'u', 'r', 'e' };
static const symbol s_1_54[3] = { 'e', 's', 'e' };
static const symbol s_1_55[4] = { 'w', 'i', 's', 'e' };
static const symbol s_1_56[3] = { 'a', 't', 'e' };
static const symbol s_1_57[7] = { 'e', 'n', 't', 'i', 'a', 't', 'e' };
static const symbol s_1_58[5] = { 'i', 'n', 'a', 't', 'e' };
static const symbol s_1_59[6] = { 'i', 'o', 'n', 'a', 't', 'e' };
static const symbol s_1_60[3] = { 'i', 't', 'e' };
static const symbol s_1_61[3] = { 'i', 'v', 'e' };
static const symbol s_1_62[5] = { 'a', 't', 'i', 'v', 'e' };
static const symbol s_1_63[3] = { 'i', 'z', 'e' };
static const symbol s_1_64[5] = { 'a', 'l', 'i', 'z', 'e' };
static const symbol s_1_65[7] = { 'i', 'c', 'a', 'l', 'i', 'z', 'e' };
static const symbol s_1_66[6] = { 'i', 'a', 'l', 'i', 'z', 'e' };
static const symbol s_1_67[9] = { 'e', 'n', 't', 'i', 'a', 'l', 'i', 'z', 'e' };
static const symbol s_1_68[8] = { 'i', 'o', 'n', 'a', 'l', 'i', 'z', 'e' };
static const symbol s_1_69[5] = { 'a', 'r', 'i', 'z', 'e' };
static const symbol s_1_70[3] = { 'i', 'n', 'g' };
static const symbol s_1_71[6] = { 'a', 'n', 'c', 'i', 'n', 'g' };
static const symbol s_1_72[6] = { 'e', 'n', 'c', 'i', 'n', 'g' };
static const symbol s_1_73[5] = { 'a', 'g', 'i', 'n', 'g' };
static const symbol s_1_74[5] = { 'e', 'n', 'i', 'n', 'g' };
static const symbol s_1_75[6] = { 'i', 'o', 'n', 'i', 'n', 'g' };
static const symbol s_1_76[5] = { 'a', 't', 'i', 'n', 'g' };
static const symbol s_1_77[6] = { 'e', 'n', 't', 'i', 'n', 'g' };
static const symbol s_1_78[4] = { 'y', 'i', 'n', 'g' };
static const symbol s_1_79[5] = { 'i', 'z', 'i', 'n', 'g' };
static const symbol s_1_80[7] = { 'a', 'r', 'i', 'z', 'i', 'n', 'g' };
static const symbol s_1_81[3] = { 'i', 's', 'h' };
static const symbol s_1_82[4] = { 'y', 'i', 's', 'h' };
static const symbol s_1_83[1] = { 'i' };
static const symbol s_1_84[2] = { 'a', 'l' };
static const symbol s_1_85[4] = { 'i', 'c', 'a', 'l' };
static const symbol s_1_86[5] = { 'a', 'i', 'c', 'a', 'l' };
static const symbol s_1_87[7] = { 'i', 's', 't', 'i', 'c', 'a', 'l' };
static const symbol s_1_88[5] = { 'o', 'i', 'd', 'a', 'l' };
static const symbol s_1_89[3] = { 'e', 'a', 'l' };
static const symbol s_1_90[3] = { 'i', 'a', 'l' };
static const symbol s_1_91[6] = { 'a', 'n', 'c', 'i', 'a', 'l' };
static const symbol s_1_92[5] = { 'a', 'r', 'i', 'a', 'l' };
static const symbol s_1_93[6] = { 'e', 'n', 't', 'i', 'a', 'l' };
static const symbol s_1_94[5] = { 'i', 'o', 'n', 'a', 'l' };
static const symbol s_1_95[7] = { 'a', 't', 'i', 'o', 'n', 'a', 'l' };
static const symbol s_1_96[9] = { 'i', 'z', 'a', 't', 'i', 'o', 'n', 'a', 'l' };
static const symbol s_1_97[5] = { 'e', 'n', 't', 'a', 'l' };
static const symbol s_1_98[3] = { 'f', 'u', 'l' };
static const symbol s_1_99[4] = { 'e', 'f', 'u', 'l' };
static const symbol s_1_100[4] = { 'i', 'f', 'u', 'l' };
static const symbol s_1_101[2] = { 'y', 'l' };
static const symbol s_1_102[3] = { 'i', 's', 'm' };
static const symbol s_1_103[5] = { 'i', 'c', 'i', 's', 'm' };
static const symbol s_1_104[6] = { 'o', 'i', 'd', 'i', 's', 'm' };
static const symbol s_1_105[5] = { 'a', 'l', 'i', 's', 'm' };
static const symbol s_1_106[7] = { 'i', 'c', 'a', 'l', 'i', 's', 'm' };
static const symbol s_1_107[8] = { 'i', 'o', 'n', 'a', 'l', 'i', 's', 'm' };
static const symbol s_1_108[5] = { 'i', 'n', 'i', 's', 'm' };
static const symbol s_1_109[7] = { 'a', 't', 'i', 'v', 'i', 's', 'm' };
static const symbol s_1_110[2] = { 'u', 'm' };
static const symbol s_1_111[3] = { 'i', 'u', 'm' };
static const symbol s_1_112[3] = { 'i', 'a', 'n' };
static const symbol s_1_113[5] = { 'i', 'c', 'i', 'a', 'n' };
static const symbol s_1_114[2] = { 'e', 'n' };
static const symbol s_1_115[4] = { 'o', 'g', 'e', 'n' };
static const symbol s_1_116[2] = { 'o', 'n' };
static const symbol s_1_117[3] = { 'i', 'o', 'n' };
static const symbol s_1_118[5] = { 'a', 't', 'i', 'o', 'n' };
static const symbol s_1_119[7] = { 'i', 'c', 'a', 't', 'i', 'o', 'n' };
static const symbol s_1_120[9] = { 'e', 'n', 't', 'i', 'a', 't', 'i', 'o', 'n' };
static const symbol s_1_121[7] = { 'i', 'n', 'a', 't', 'i', 'o', 'n' };
static const symbol s_1_122[7] = { 'i', 's', 'a', 't', 'i', 'o', 'n' };
static const symbol s_1_123[9] = { 'a', 'r', 'i', 's', 'a', 't', 'i', 'o', 'n' };
static const symbol s_1_124[8] = { 'e', 'n', 't', 'a', 't', 'i', 'o', 'n' };
static const symbol s_1_125[7] = { 'i', 'z', 'a', 't', 'i', 'o', 'n' };
static const symbol s_1_126[9] = { 'a', 'r', 'i', 'z', 'a', 't', 'i', 'o', 'n' };
static const symbol s_1_127[6] = { 'a', 'c', 't', 'i', 'o', 'n' };
static const symbol s_1_128[1] = { 'o' };
static const symbol s_1_129[2] = { 'a', 'r' };
static const symbol s_1_130[3] = { 'e', 'a', 'r' };
static const symbol s_1_131[3] = { 'i', 'e', 'r' };
static const symbol s_1_132[6] = { 'a', 'r', 'i', 's', 'e', 'r' };
static const symbol s_1_133[4] = { 'i', 'z', 'e', 'r' };
static const symbol s_1_134[6] = { 'a', 'r', 'i', 'z', 'e', 'r' };
static const symbol s_1_135[2] = { 'o', 'r' };
static const symbol s_1_136[4] = { 'a', 't', 'o', 'r' };
static const symbol s_1_137[1] = { 's' };
static const symbol s_1_138[2] = { '\'', 's' };
static const symbol s_1_139[2] = { 'a', 's' };
static const symbol s_1_140[3] = { 'i', 'c', 's' };
static const symbol s_1_141[6] = { 'i', 's', 't', 'i', 'c', 's' };
static const symbol s_1_142[2] = { 'e', 's' };
static const symbol s_1_143[5] = { 'a', 'n', 'c', 'e', 's' };
static const symbol s_1_144[5] = { 'e', 'n', 'c', 'e', 's' };
static const symbol s_1_145[4] = { 'i', 'd', 'e', 's' };
static const symbol s_1_146[5] = { 'o', 'i', 'd', 'e', 's' };
static const symbol s_1_147[4] = { 'a', 'g', 'e', 's' };
static const symbol s_1_148[3] = { 'i', 'e', 's' };
static const symbol s_1_149[5] = { 'a', 'c', 'i', 'e', 's' };
static const symbol s_1_150[6] = { 'a', 'n', 'c', 'i', 'e', 's' };
static const symbol s_1_151[6] = { 'e', 'n', 'c', 'i', 'e', 's' };
static const symbol s_1_152[5] = { 'a', 'r', 'i', 'e', 's' };
static const symbol s_1_153[5] = { 'i', 't', 'i', 'e', 's' };
static const symbol s_1_154[7] = { 'a', 'l', 'i', 't', 'i', 'e', 's' };
static const symbol s_1_155[7] = { 'i', 'v', 'i', 't', 'i', 'e', 's' };
static const symbol s_1_156[4] = { 'i', 'n', 'e', 's' };
static const symbol s_1_157[6] = { 'n', 'e', 's', 's', 'e', 's' };
static const symbol s_1_158[4] = { 'a', 't', 'e', 's' };
static const symbol s_1_159[6] = { 'a', 't', 'i', 'v', 'e', 's' };
static const symbol s_1_160[4] = { 'i', 'n', 'g', 's' };
static const symbol s_1_161[2] = { 'i', 's' };
static const symbol s_1_162[3] = { 'a', 'l', 's' };
static const symbol s_1_163[4] = { 'i', 'a', 'l', 's' };
static const symbol s_1_164[7] = { 'e', 'n', 't', 'i', 'a', 'l', 's' };
static const symbol s_1_165[6] = { 'i', 'o', 'n', 'a', 'l', 's' };
static const symbol s_1_166[4] = { 'i', 's', 'm', 's' };
static const symbol s_1_167[4] = { 'i', 'a', 'n', 's' };
static const symbol s_1_168[6] = { 'i', 'c', 'i', 'a', 'n', 's' };
static const symbol s_1_169[4] = { 'i', 'o', 'n', 's' };
static const symbol s_1_170[6] = { 'a', 't', 'i', 'o', 'n', 's' };
static const symbol s_1_171[10] = { 'a', 'r', 'i', 's', 'a', 't', 'i', 'o', 'n', 's' };
static const symbol s_1_172[9] = { 'e', 'n', 't', 'a', 't', 'i', 'o', 'n', 's' };
static const symbol s_1_173[8] = { 'i', 'z', 'a', 't', 'i', 'o', 'n', 's' };
static const symbol s_1_174[10] = { 'a', 'r', 'i', 'z', 'a', 't', 'i', 'o', 'n', 's' };
static const symbol s_1_175[3] = { 'a', 'r', 's' };
static const symbol s_1_176[4] = { 'i', 'e', 'r', 's' };
static const symbol s_1_177[5] = { 'i', 'z', 'e', 'r', 's' };
static const symbol s_1_178[5] = { 'a', 't', 'o', 'r', 's' };
static const symbol s_1_179[4] = { 'l', 'e', 's', 's' };
static const symbol s_1_180[5] = { 'e', 'l', 'e', 's', 's' };
static const symbol s_1_181[4] = { 'n', 'e', 's', 's' };
static const symbol s_1_182[5] = { 'e', 'n', 'e', 's', 's' };
static const symbol s_1_183[8] = { 'a', 'b', 'l', 'e', 'n', 'e', 's', 's' };
static const symbol s_1_184[9] = { 'e', 'a', 'b', 'l', 'e', 'n', 'e', 's', 's' };
static const symbol s_1_185[8] = { 'i', 'b', 'l', 'e', 'n', 'e', 's', 's' };
static const symbol s_1_186[7] = { 'a', 't', 'e', 'n', 'e', 's', 's' };
static const symbol s_1_187[7] = { 'i', 't', 'e', 'n', 'e', 's', 's' };
static const symbol s_1_188[7] = { 'i', 'v', 'e', 'n', 'e', 's', 's' };
static const symbol s_1_189[9] = { 'a', 't', 'i', 'v', 'e', 'n', 'e', 's', 's' };
static const symbol s_1_190[7] = { 'i', 'n', 'g', 'n', 'e', 's', 's' };
static const symbol s_1_191[7] = { 'i', 's', 'h', 'n', 'e', 's', 's' };
static const symbol s_1_192[5] = { 'i', 'n', 'e', 's', 's' };
static const symbol s_1_193[7] = { 'a', 'r', 'i', 'n', 'e', 's', 's' };
static const symbol s_1_194[6] = { 'a', 'l', 'n', 'e', 's', 's' };
static const symbol s_1_195[8] = { 'i', 'c', 'a', 'l', 'n', 'e', 's', 's' };
static const symbol s_1_196[10] = { 'a', 'n', 't', 'i', 'a', 'l', 'n', 'e', 's', 's' };
static const symbol s_1_197[10] = { 'e', 'n', 't', 'i', 'a', 'l', 'n', 'e', 's', 's' };
static const symbol s_1_198[9] = { 'i', 'o', 'n', 'a', 'l', 'n', 'e', 's', 's' };
static const symbol s_1_199[7] = { 'f', 'u', 'l', 'n', 'e', 's', 's' };
static const symbol s_1_200[8] = { 'l', 'e', 's', 's', 'n', 'e', 's', 's' };
static const symbol s_1_201[7] = { 'o', 'u', 's', 'n', 'e', 's', 's' };
static const symbol s_1_202[8] = { 'e', 'o', 'u', 's', 'n', 'e', 's', 's' };
static const symbol s_1_203[8] = { 'i', 'o', 'u', 's', 'n', 'e', 's', 's' };
static const symbol s_1_204[9] = { 'i', 't', 'o', 'u', 's', 'n', 'e', 's', 's' };
static const symbol s_1_205[7] = { 'e', 'n', 't', 'n', 'e', 's', 's' };
static const symbol s_1_206[4] = { 'a', 'n', 't', 's' };
static const symbol s_1_207[4] = { 'i', 's', 't', 's' };
static const symbol s_1_208[6] = { 'i', 'c', 'i', 's', 't', 's' };
static const symbol s_1_209[2] = { 'u', 's' };
static const symbol s_1_210[3] = { 'o', 'u', 's' };
static const symbol s_1_211[4] = { 'e', 'o', 'u', 's' };
static const symbol s_1_212[6] = { 'a', 'c', 'e', 'o', 'u', 's' };
static const symbol s_1_213[9] = { 'a', 'n', 't', 'a', 'n', 'e', 'o', 'u', 's' };
static const symbol s_1_214[4] = { 'i', 'o', 'u', 's' };
static const symbol s_1_215[6] = { 'a', 'c', 'i', 'o', 'u', 's' };
static const symbol s_1_216[5] = { 'i', 't', 'o', 'u', 's' };
static const symbol s_1_217[3] = { 'a', 'n', 't' };
static const symbol s_1_218[5] = { 'i', 'c', 'a', 'n', 't' };
static const symbol s_1_219[3] = { 'e', 'n', 't' };
static const symbol s_1_220[5] = { 'e', 'm', 'e', 'n', 't' };
static const symbol s_1_221[7] = { 'i', 'z', 'e', 'm', 'e', 'n', 't' };
static const symbol s_1_222[3] = { 'i', 's', 't' };
static const symbol s_1_223[5] = { 'i', 'c', 'i', 's', 't' };
static const symbol s_1_224[5] = { 'a', 'l', 'i', 's', 't' };
static const symbol s_1_225[7] = { 'i', 'c', 'a', 'l', 'i', 's', 't' };
static const symbol s_1_226[6] = { 'i', 'a', 'l', 'i', 's', 't' };
static const symbol s_1_227[6] = { 'i', 'o', 'n', 'i', 's', 't' };
static const symbol s_1_228[6] = { 'e', 'n', 't', 'i', 's', 't' };
static const symbol s_1_229[1] = { 'y' };
static const symbol s_1_230[3] = { 'a', 'c', 'y' };
static const symbol s_1_231[4] = { 'a', 'n', 'c', 'y' };
static const symbol s_1_232[4] = { 'e', 'n', 'c', 'y' };
static const symbol s_1_233[2] = { 'l', 'y' };
static const symbol s_1_234[4] = { 'e', 'a', 'l', 'y' };
static const symbol s_1_235[4] = { 'a', 'b', 'l', 'y' };
static const symbol s_1_236[4] = { 'i', 'b', 'l', 'y' };
static const symbol s_1_237[4] = { 'e', 'd', 'l', 'y' };
static const symbol s_1_238[5] = { 'i', 'e', 'd', 'l', 'y' };
static const symbol s_1_239[3] = { 'e', 'l', 'y' };
static const symbol s_1_240[5] = { 'a', 't', 'e', 'l', 'y' };
static const symbol s_1_241[5] = { 'i', 'v', 'e', 'l', 'y' };
static const symbol s_1_242[7] = { 'a', 't', 'i', 'v', 'e', 'l', 'y' };
static const symbol s_1_243[5] = { 'i', 'n', 'g', 'l', 'y' };
static const symbol s_1_244[7] = { 'a', 't', 'i', 'n', 'g', 'l', 'y' };
static const symbol s_1_245[3] = { 'i', 'l', 'y' };
static const symbol s_1_246[4] = { 'l', 'i', 'l', 'y' };
static const symbol s_1_247[5] = { 'a', 'r', 'i', 'l', 'y' };
static const symbol s_1_248[4] = { 'a', 'l', 'l', 'y' };
static const symbol s_1_249[6] = { 'i', 'c', 'a', 'l', 'l', 'y' };
static const symbol s_1_250[7] = { 'a', 'i', 'c', 'a', 'l', 'l', 'y' };
static const symbol s_1_251[9] = { 'a', 'l', 'l', 'i', 'c', 'a', 'l', 'l', 'y' };
static const symbol s_1_252[9] = { 'i', 's', 't', 'i', 'c', 'a', 'l', 'l', 'y' };
static const symbol s_1_253[11] = { 'a', 'l', 'i', 's', 't', 'i', 'c', 'a', 'l', 'l', 'y' };
static const symbol s_1_254[7] = { 'o', 'i', 'd', 'a', 'l', 'l', 'y' };
static const symbol s_1_255[5] = { 'i', 'a', 'l', 'l', 'y' };
static const symbol s_1_256[8] = { 'e', 'n', 't', 'i', 'a', 'l', 'l', 'y' };
static const symbol s_1_257[7] = { 'i', 'o', 'n', 'a', 'l', 'l', 'y' };
static const symbol s_1_258[9] = { 'a', 't', 'i', 'o', 'n', 'a', 'l', 'l', 'y' };
static const symbol s_1_259[11] = { 'i', 'z', 'a', 't', 'i', 'o', 'n', 'a', 'l', 'l', 'y' };
static const symbol s_1_260[7] = { 'e', 'n', 't', 'a', 'l', 'l', 'y' };
static const symbol s_1_261[5] = { 'f', 'u', 'l', 'l', 'y' };
static const symbol s_1_262[6] = { 'e', 'f', 'u', 'l', 'l', 'y' };
static const symbol s_1_263[6] = { 'i', 'f', 'u', 'l', 'l', 'y' };
static const symbol s_1_264[4] = { 'e', 'n', 'l', 'y' };
static const symbol s_1_265[4] = { 'a', 'r', 'l', 'y' };
static const symbol s_1_266[5] = { 'e', 'a', 'r', 'l', 'y' };
static const symbol s_1_267[6] = { 'l', 'e', 's', 's', 'l', 'y' };
static const symbol s_1_268[5] = { 'o', 'u', 's', 'l', 'y' };
static const symbol s_1_269[6] = { 'e', 'o', 'u', 's', 'l', 'y' };
static const symbol s_1_270[6] = { 'i', 'o', 'u', 's', 'l', 'y' };
static const symbol s_1_271[5] = { 'e', 'n', 't', 'l', 'y' };
static const symbol s_1_272[3] = { 'a', 'r', 'y' };
static const symbol s_1_273[3] = { 'e', 'r', 'y' };
static const symbol s_1_274[7] = { 'i', 'c', 'i', 'a', 'n', 'r', 'y' };
static const symbol s_1_275[5] = { 'a', 't', 'o', 'r', 'y' };
static const symbol s_1_276[3] = { 'i', 't', 'y' };
static const symbol s_1_277[5] = { 'a', 'c', 'i', 't', 'y' };
static const symbol s_1_278[5] = { 'i', 'c', 'i', 't', 'y' };
static const symbol s_1_279[4] = { 'e', 'i', 't', 'y' };
static const symbol s_1_280[5] = { 'a', 'l', 'i', 't', 'y' };
static const symbol s_1_281[7] = { 'i', 'c', 'a', 'l', 'i', 't', 'y' };
static const symbol s_1_282[6] = { 'i', 'a', 'l', 'i', 't', 'y' };
static const symbol s_1_283[9] = { 'a', 'n', 't', 'i', 'a', 'l', 'i', 't', 'y' };
static const symbol s_1_284[9] = { 'e', 'n', 't', 'i', 'a', 'l', 'i', 't', 'y' };
static const symbol s_1_285[8] = { 'i', 'o', 'n', 'a', 'l', 'i', 't', 'y' };
static const symbol s_1_286[5] = { 'e', 'l', 'i', 't', 'y' };
static const symbol s_1_287[7] = { 'a', 'b', 'i', 'l', 'i', 't', 'y' };
static const symbol s_1_288[9] = { 'i', 'z', 'a', 'b', 'i', 'l', 'i', 't', 'y' };
static const symbol s_1_289[11] = { 'a', 'r', 'i', 'z', 'a', 'b', 'i', 'l', 'i', 't', 'y' };
static const symbol s_1_290[7] = { 'i', 'b', 'i', 'l', 'i', 't', 'y' };
static const symbol s_1_291[5] = { 'i', 'n', 'i', 't', 'y' };
static const symbol s_1_292[5] = { 'a', 'r', 'i', 't', 'y' };
static const symbol s_1_293[5] = { 'i', 'v', 'i', 't', 'y' };
static const struct among a_1[294] = {
{ 2, s_1_0, 0, 1, 2},
{ 1, s_1_1, 0, 1, 2},
{ 2, s_1_2, -1, 1, 2},
{ 3, s_1_3, -2, 1, 2},
{ 2, s_1_4, 0, 1, 2},
{ 3, s_1_5, -1, 1, 2},
{ 5, s_1_6, -2, 1, 9},
{ 4, s_1_7, -3, 1, 2},
{ 4, s_1_8, -4, 1, 1},
{ 4, s_1_9, -5, 1, 18},
{ 5, s_1_10, -6, 1, 3},
{ 5, s_1_11, -7, 1, 2},
{ 7, s_1_12, -1, 1, 1},
{ 7, s_1_13, -2, 1, 2},
{ 7, s_1_14, -3, 1, 2},
{ 2, s_1_15, 0, 1, 4},
{ 5, s_1_16, -1, 1, 1},
{ 5, s_1_17, -2, 1, 2},
{ 5, s_1_18, -3, 1, 2},
{ 3, s_1_19, -4, 1, 2},
{ 4, s_1_20, -5, 1, 4},
{ 5, s_1_21, -6, 1, 2},
{ 4, s_1_22, -7, 1, 10},
{ 5, s_1_23, -8, 1, 3},
{ 4, s_1_24, -9, 1, 6},
{ 6, s_1_25, -1, 1, 2},
{ 3, s_1_26, 0, 1, 2},
{ 5, s_1_27, -1, 1, 2},
{ 4, s_1_28, 0, 1, 2},
{ 5, s_1_29, -1, 1, 2},
{ 5, s_1_30, -2, 1, 2},
{ 7, s_1_31, -1, 1, 4},
{ 4, s_1_32, 0, 1, 2},
{ 1, s_1_33, 0, 1, 2},
{ 2, s_1_34, -1, 1, 2},
{ 4, s_1_35, -2, 1, 1},
{ 6, s_1_36, -1, 1, 2},
{ 4, s_1_37, -4, 1, 2},
{ 3, s_1_38, -5, 1, 15},
{ 5, s_1_39, -1, 1, 2},
{ 5, s_1_40, -2, 1, 2},
{ 3, s_1_41, -8, 1, 1},
{ 4, s_1_42, -9, 1, 2},
{ 6, s_1_43, -1, 1, 2},
{ 6, s_1_44, -2, 1, 4},
{ 8, s_1_45, -1, 1, 2},
{ 4, s_1_46, -13, 1, 2},
{ 7, s_1_47, -1, 1, 2},
{ 3, s_1_48, -15, 1, 4},
{ 3, s_1_49, -16, 1, 16},
{ 5, s_1_50, -1, 1, 10},
{ 3, s_1_51, -18, 1, 23},
{ 5, s_1_52, -19, 1, 4},
{ 6, s_1_53, -1, 1, 7},
{ 3, s_1_54, -21, 1, 2},
{ 4, s_1_55, -22, 1, 2},
{ 3, s_1_56, -23, 1, 2},
{ 7, s_1_57, -1, 1, 2},
{ 5, s_1_58, -2, 1, 2},
{ 6, s_1_59, -3, 1, 8},
{ 3, s_1_60, -27, 1, 22},
{ 3, s_1_61, -28, 1, 2},
{ 5, s_1_62, -1, 1, 2},
{ 3, s_1_63, -30, 1, 6},
{ 5, s_1_64, -1, 1, 2},
{ 7, s_1_65, -1, 1, 2},
{ 6, s_1_66, -2, 1, 2},
{ 9, s_1_67, -1, 1, 2},
{ 8, s_1_68, -4, 1, 2},
{ 5, s_1_69, -6, 1, 2},
{ 3, s_1_70, 0, 1, 17},
{ 6, s_1_71, -1, 1, 1},
{ 6, s_1_72, -2, 1, 2},
{ 5, s_1_73, -3, 1, 1},
{ 5, s_1_74, -4, 1, 4},
{ 6, s_1_75, -5, 1, 2},
{ 5, s_1_76, -6, 1, 10},
{ 6, s_1_77, -7, 1, 3},
{ 4, s_1_78, -8, 1, 1},
{ 5, s_1_79, -9, 1, 6},
{ 7, s_1_80, -1, 1, 2},
{ 3, s_1_81, 0, 1, 3},
{ 4, s_1_82, -1, 1, 2},
{ 1, s_1_83, 0, 1, 2},
{ 2, s_1_84, 0, 1, 9},
{ 4, s_1_85, -1, 1, 2},
{ 5, s_1_86, -1, 1, 2},
{ 7, s_1_87, -2, 1, 2},
{ 5, s_1_88, -4, 1, 2},
{ 3, s_1_89, -5, 1, 11},
{ 3, s_1_90, -6, 1, 2},
{ 6, s_1_91, -1, 1, 2},
{ 5, s_1_92, -2, 1, 2},
{ 6, s_1_93, -3, 1, 2},
{ 5, s_1_94, -10, 1, 2},
{ 7, s_1_95, -1, 1, 1},
{ 9, s_1_96, -1, 1, 2},
{ 5, s_1_97, -13, 1, 2},
{ 3, s_1_98, 0, 1, 2},
{ 4, s_1_99, -1, 1, 2},
{ 4, s_1_100, -2, 1, 2},
{ 2, s_1_101, 0, 1, 23},
{ 3, s_1_102, 0, 1, 1},
{ 5, s_1_103, -1, 1, 2},
{ 6, s_1_104, -2, 1, 2},
{ 5, s_1_105, -3, 1, 1},
{ 7, s_1_106, -1, 1, 2},
{ 8, s_1_107, -2, 1, 2},
{ 5, s_1_108, -6, 1, 12},
{ 7, s_1_109, -7, 1, 2},
{ 2, s_1_110, 0, 1, 27},
{ 3, s_1_111, -1, 1, 2},
{ 3, s_1_112, 0, 1, 2},
{ 5, s_1_113, -1, 1, 2},
{ 2, s_1_114, 0, 1, 6},
{ 4, s_1_115, -1, 1, 2},
{ 2, s_1_116, 0, 1, 25},
{ 3, s_1_117, -1, 1, 21},
{ 5, s_1_118, -1, 1, 1},
{ 7, s_1_119, -1, 1, 5},
{ 9, s_1_120, -2, 1, 2},
{ 7, s_1_121, -3, 1, 2},
{ 7, s_1_122, -4, 1, 2},
{ 9, s_1_123, -1, 1, 2},
{ 8, s_1_124, -6, 1, 2},
{ 7, s_1_125, -7, 1, 6},
{ 9, s_1_126, -1, 1, 2},
{ 6, s_1_127, -10, 1, 5},
{ 1, s_1_128, 0, 1, 2},
{ 2, s_1_129, 0, 1, 24},
{ 3, s_1_130, -1, 1, 11},
{ 3, s_1_131, 0, 1, 2},
{ 6, s_1_132, 0, 1, 2},
{ 4, s_1_133, 0, 1, 6},
{ 6, s_1_134, -1, 1, 2},
{ 2, s_1_135, 0, 1, 26},
{ 4, s_1_136, -1, 1, 2},
{ 1, s_1_137, 0, 1, 29},
{ 2, s_1_138, -1, 1, 2},
{ 2, s_1_139, -2, 1, 1},
{ 3, s_1_140, -3, 1, 2},
{ 6, s_1_141, -1, 1, 2},
{ 2, s_1_142, -5, 1, 4},
{ 5, s_1_143, -1, 1, 1},
{ 5, s_1_144, -2, 1, 2},
{ 4, s_1_145, -3, 1, 15},
{ 5, s_1_146, -1, 1, 2},
{ 4, s_1_147, -5, 1, 1},
{ 3, s_1_148, -6, 1, 20},
{ 5, s_1_149, -1, 1, 2},
{ 6, s_1_150, -2, 1, 2},
{ 6, s_1_151, -3, 1, 2},
{ 5, s_1_152, -4, 1, 2},
{ 5, s_1_153, -5, 1, 2},
{ 7, s_1_154, -1, 1, 2},
{ 7, s_1_155, -2, 1, 2},
{ 4, s_1_156, -14, 1, 16},
{ 6, s_1_157, -15, 1, 2},
{ 4, s_1_158, -16, 1, 2},
{ 6, s_1_159, -17, 1, 2},
{ 4, s_1_160, -23, 1, 17},
{ 2, s_1_161, -24, 1, 2},
{ 3, s_1_162, -25, 1, 9},
{ 4, s_1_163, -1, 1, 2},
{ 7, s_1_164, -1, 1, 2},
{ 6, s_1_165, -3, 1, 2},
{ 4, s_1_166, -29, 1, 1},
{ 4, s_1_167, -30, 1, 2},
{ 6, s_1_168, -1, 1, 2},
{ 4, s_1_169, -32, 1, 1},
{ 6, s_1_170, -1, 1, 1},
{ 10, s_1_171, -1, 1, 2},
{ 9, s_1_172, -2, 1, 2},
{ 8, s_1_173, -3, 1, 2},
{ 10, s_1_174, -1, 1, 2},
{ 3, s_1_175, -38, 1, 19},
{ 4, s_1_176, -39, 1, 2},
{ 5, s_1_177, -40, 1, 6},
{ 5, s_1_178, -41, 1, 2},
{ 4, s_1_179, -42, 1, 2},
{ 5, s_1_180, -1, 1, 2},
{ 4, s_1_181, -44, 1, 2},
{ 5, s_1_182, -1, 1, 4},
{ 8, s_1_183, -1, 1, 2},
{ 9, s_1_184, -1, 1, 4},
{ 8, s_1_185, -3, 1, 2},
{ 7, s_1_186, -4, 1, 2},
{ 7, s_1_187, -5, 1, 2},
{ 7, s_1_188, -6, 1, 2},
{ 9, s_1_189, -1, 1, 2},
{ 7, s_1_190, -9, 1, 2},
{ 7, s_1_191, -10, 1, 2},
{ 5, s_1_192, -11, 1, 2},
{ 7, s_1_193, -1, 1, 4},
{ 6, s_1_194, -13, 1, 2},
{ 8, s_1_195, -1, 1, 2},
{ 10, s_1_196, -2, 1, 2},
{ 10, s_1_197, -3, 1, 2},
{ 9, s_1_198, -4, 1, 2},
{ 7, s_1_199, -18, 1, 2},
{ 8, s_1_200, -19, 1, 2},
{ 7, s_1_201, -20, 1, 2},
{ 8, s_1_202, -1, 1, 2},
{ 8, s_1_203, -2, 1, 2},
{ 9, s_1_204, -3, 1, 2},
{ 7, s_1_205, -24, 1, 2},
{ 4, s_1_206, -69, 1, 1},
{ 4, s_1_207, -70, 1, 2},
{ 6, s_1_208, -1, 1, 2},
{ 2, s_1_209, -72, 1, 28},
{ 3, s_1_210, -1, 1, 2},
{ 4, s_1_211, -1, 1, 2},
{ 6, s_1_212, -1, 1, 2},
{ 9, s_1_213, -2, 1, 2},
{ 4, s_1_214, -4, 1, 2},
{ 6, s_1_215, -1, 1, 1},
{ 5, s_1_216, -6, 1, 2},
{ 3, s_1_217, 0, 1, 1},
{ 5, s_1_218, -1, 1, 2},
{ 3, s_1_219, 0, 1, 3},
{ 5, s_1_220, -1, 1, 2},
{ 7, s_1_221, -1, 1, 2},
{ 3, s_1_222, 0, 1, 2},
{ 5, s_1_223, -1, 1, 2},
{ 5, s_1_224, -2, 1, 2},
{ 7, s_1_225, -1, 1, 2},
{ 6, s_1_226, -2, 1, 2},
{ 6, s_1_227, -5, 1, 2},
{ 6, s_1_228, -6, 1, 2},
{ 1, s_1_229, 0, 1, 1},
{ 3, s_1_230, -1, 1, 2},
{ 4, s_1_231, -2, 1, 1},
{ 4, s_1_232, -3, 1, 2},
{ 2, s_1_233, -4, 1, 1},
{ 4, s_1_234, -1, 1, 11},
{ 4, s_1_235, -2, 1, 2},
{ 4, s_1_236, -3, 1, 2},
{ 4, s_1_237, -4, 1, 4},
{ 5, s_1_238, -1, 1, 2},
{ 3, s_1_239, -6, 1, 4},
{ 5, s_1_240, -1, 1, 2},
{ 5, s_1_241, -2, 1, 2},
{ 7, s_1_242, -1, 1, 2},
{ 5, s_1_243, -10, 1, 1},
{ 7, s_1_244, -1, 1, 2},
{ 3, s_1_245, -12, 1, 2},
{ 4, s_1_246, -1, 1, 2},
{ 5, s_1_247, -2, 1, 2},
{ 4, s_1_248, -15, 1, 1},
{ 6, s_1_249, -1, 1, 2},
{ 7, s_1_250, -1, 1, 2},
{ 9, s_1_251, -2, 1, 3},
{ 9, s_1_252, -3, 1, 2},
{ 11, s_1_253, -1, 1, 1},
{ 7, s_1_254, -6, 1, 2},
{ 5, s_1_255, -7, 1, 2},
{ 8, s_1_256, -1, 1, 2},
{ 7, s_1_257, -9, 1, 2},
{ 9, s_1_258, -1, 1, 1},
{ 11, s_1_259, -1, 1, 1},
{ 7, s_1_260, -12, 1, 2},
{ 5, s_1_261, -28, 1, 2},
{ 6, s_1_262, -1, 1, 2},
{ 6, s_1_263, -2, 1, 2},
{ 4, s_1_264, -31, 1, 4},
{ 4, s_1_265, -32, 1, 14},
{ 5, s_1_266, -1, 1, 11},
{ 6, s_1_267, -34, 1, 2},
{ 5, s_1_268, -35, 1, 2},
{ 6, s_1_269, -1, 1, 2},
{ 6, s_1_270, -2, 1, 2},
{ 5, s_1_271, -38, 1, 2},
{ 3, s_1_272, -43, 1, 6},
{ 3, s_1_273, -44, 1, 4},
{ 7, s_1_274, -45, 1, 2},
{ 5, s_1_275, -46, 1, 2},
{ 3, s_1_276, -47, 1, 2},
{ 5, s_1_277, -1, 1, 2},
{ 5, s_1_278, -2, 1, 2},
{ 4, s_1_279, -3, 1, 2},
{ 5, s_1_280, -4, 1, 2},
{ 7, s_1_281, -1, 1, 2},
{ 6, s_1_282, -2, 1, 2},
{ 9, s_1_283, -1, 1, 2},
{ 9, s_1_284, -2, 1, 2},
{ 8, s_1_285, -5, 1, 2},
{ 5, s_1_286, -10, 1, 2},
{ 7, s_1_287, -11, 1, 2},
{ 9, s_1_288, -1, 1, 2},
{ 11, s_1_289, -1, 1, 2},
{ 7, s_1_290, -14, 1, 2},
{ 5, s_1_291, -15, 1, 13},
{ 5, s_1_292, -16, 1, 1},
{ 5, s_1_293, -17, 1, 2}
};

static int af_1(struct SN_env * z) {
    switch (z->af) {
        case 1: return r_B(z);
        case 2: return r_A(z);
        case 3: return r_C(z);
        case 4: return r_E(z);
        case 5: return r_G(z);
        case 6: return r_F(z);
        case 7: return r_Z(z);
        case 8: return r_D(z);
        case 9: return r_BB(z);
        case 10: return r_I(z);
        case 11: return r_Y(z);
        case 12: return r_J(z);
        case 13: return r_CC(z);
        case 14: return r_K(z);
        case 15: return r_L(z);
        case 16: return r_M(z);
        case 17: return r_N(z);
        case 18: return r_H(z);
        case 19: return r_O(z);
        case 20: return r_P(z);
        case 21: return r_Q(z);
        case 22: return r_AA(z);
        case 23: return r_R(z);
        case 24: return r_X(z);
        case 25: return r_S(z);
        case 26: return r_T(z);
        case 27: return r_U(z);
        case 28: return r_V(z);
        case 29: return r_W(z);
    }
    return -1;
}

static const symbol s_2_0[2] = { 'b', 'b' };
static const symbol s_2_1[2] = { 'd', 'd' };
static const symbol s_2_2[2] = { 'g', 'g' };
static const symbol s_2_3[2] = { 'l', 'l' };
static const symbol s_2_4[2] = { 'm', 'm' };
static const symbol s_2_5[2] = { 'n', 'n' };
static const symbol s_2_6[2] = { 'p', 'p' };
static const symbol s_2_7[2] = { 'r', 'r' };
static const symbol s_2_8[2] = { 's', 's' };
static const symbol s_2_9[2] = { 't', 't' };
static const struct among a_2[10] = {
{ 2, s_2_0, 0, -1, 0},
{ 2, s_2_1, 0, -1, 0},
{ 2, s_2_2, 0, -1, 0},
{ 2, s_2_3, 0, -1, 0},
{ 2, s_2_4, 0, -1, 0},
{ 2, s_2_5, 0, -1, 0},
{ 2, s_2_6, 0, -1, 0},
{ 2, s_2_7, 0, -1, 0},
{ 2, s_2_8, 0, -1, 0},
{ 2, s_2_9, 0, -1, 0}
};

static const symbol s_3_0[3] = { 'u', 'a', 'd' };
static const symbol s_3_1[3] = { 'v', 'a', 'd' };
static const symbol s_3_2[3] = { 'c', 'i', 'd' };
static const symbol s_3_3[3] = { 'l', 'i', 'd' };
static const symbol s_3_4[4] = { 'e', 'r', 'i', 'd' };
static const symbol s_3_5[4] = { 'p', 'a', 'n', 'd' };
static const symbol s_3_6[3] = { 'e', 'n', 'd' };
static const symbol s_3_7[3] = { 'o', 'n', 'd' };
static const symbol s_3_8[3] = { 'l', 'u', 'd' };
static const symbol s_3_9[3] = { 'r', 'u', 'd' };
static const symbol s_3_10[2] = { 'u', 'l' };
static const symbol s_3_11[3] = { 'h', 'e', 'r' };
static const symbol s_3_12[4] = { 'm', 'e', 't', 'r' };
static const symbol s_3_13[4] = { 'i', 's', 't', 'r' };
static const symbol s_3_14[3] = { 'u', 'r', 's' };
static const symbol s_3_15[3] = { 'u', 'c', 't' };
static const symbol s_3_16[2] = { 'e', 't' };
static const symbol s_3_17[3] = { 'm', 'i', 't' };
static const symbol s_3_18[3] = { 'e', 'n', 't' };
static const symbol s_3_19[4] = { 'u', 'm', 'p', 't' };
static const symbol s_3_20[3] = { 'r', 'p', 't' };
static const symbol s_3_21[3] = { 'e', 'r', 't' };
static const symbol s_3_22[2] = { 'y', 't' };
static const symbol s_3_23[3] = { 'i', 'e', 'v' };
static const symbol s_3_24[3] = { 'o', 'l', 'v' };
static const symbol s_3_25[2] = { 'a', 'x' };
static const symbol s_3_26[2] = { 'e', 'x' };
static const symbol s_3_27[3] = { 'b', 'e', 'x' };
static const symbol s_3_28[3] = { 'd', 'e', 'x' };
static const symbol s_3_29[3] = { 'p', 'e', 'x' };
static const symbol s_3_30[3] = { 't', 'e', 'x' };
static const symbol s_3_31[2] = { 'i', 'x' };
static const symbol s_3_32[3] = { 'l', 'u', 'x' };
static const symbol s_3_33[2] = { 'y', 'z' };
static const struct among a_3[34] = {
{ 3, s_3_0, 0, 18, 0},
{ 3, s_3_1, 0, 19, 0},
{ 3, s_3_2, 0, 20, 0},
{ 3, s_3_3, 0, 21, 0},
{ 4, s_3_4, 0, 22, 0},
{ 4, s_3_5, 0, 23, 0},
{ 3, s_3_6, 0, 24, 0},
{ 3, s_3_7, 0, 25, 0},
{ 3, s_3_8, 0, 26, 0},
{ 3, s_3_9, 0, 27, 0},
{ 2, s_3_10, 0, 9, 0},
{ 3, s_3_11, 0, 28, 0},
{ 4, s_3_12, 0, 7, 0},
{ 4, s_3_13, 0, 6, 0},
{ 3, s_3_14, 0, 5, 0},
{ 3, s_3_15, 0, 2, 0},
{ 2, s_3_16, 0, 32, 0},
{ 3, s_3_17, 0, 29, 0},
{ 3, s_3_18, 0, 30, 0},
{ 4, s_3_19, 0, 3, 0},
{ 3, s_3_20, 0, 4, 0},
{ 3, s_3_21, 0, 31, 0},
{ 2, s_3_22, 0, 33, 0},
{ 3, s_3_23, 0, 1, 0},
{ 3, s_3_24, 0, 8, 0},
{ 2, s_3_25, 0, 14, 0},
{ 2, s_3_26, 0, 15, 0},
{ 3, s_3_27, -1, 10, 0},
{ 3, s_3_28, -2, 11, 0},
{ 3, s_3_29, -3, 12, 0},
{ 3, s_3_30, -4, 13, 0},
{ 2, s_3_31, 0, 16, 0},
{ 3, s_3_32, 0, 17, 0},
{ 2, s_3_33, 0, 33, 0}
};

static int r_A(struct SN_env * z) {
    {
        int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
        if (ret < 0) return 0;
        z->c = ret;
    }
    return 1;
}

static int r_B(struct SN_env * z) {
    {
        int ret = skip_b_utf8(z->p, z->c, z->lb, 3);
        if (ret < 0) return 0;
        z->c = ret;
    }
    return 1;
}

static int r_C(struct SN_env * z) {
    {
        int ret = skip_b_utf8(z->p, z->c, z->lb, 4);
        if (ret < 0) return 0;
        z->c = ret;
    }
    return 1;
}

static int r_D(struct SN_env * z) {
    {
        int ret = skip_b_utf8(z->p, z->c, z->lb, 5);
        if (ret < 0) return 0;
        z->c = ret;
    }
    return 1;
}

static int r_E(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'e') goto lab0;
    z->c--;
    return 0;
lab0:
    return 1;
}

static int r_F(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 3);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'e') goto lab0;
    z->c--;
    return 0;
lab0:
    return 1;
}

static int r_G(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 3);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'f') return 0;
    z->c--;
    return 1;
}

static int r_H(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    do {
        if (z->c <= z->lb || z->p[z->c - 1] != 't') goto lab0;
        z->c--;
        break;
    lab0:
        if (!(eq_s_b(z, 2, s_0))) return 0;
    } while (0);
    return 1;
}

static int r_I(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'o') goto lab0;
    z->c--;
    return 0;
lab0:
    if (z->c <= z->lb || z->p[z->c - 1] != 'e') goto lab1;
    z->c--;
    return 0;
lab1:
    return 1;
}

static int r_J(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'a') goto lab0;
    z->c--;
    return 0;
lab0:
    if (z->c <= z->lb || z->p[z->c - 1] != 'e') goto lab1;
    z->c--;
    return 0;
lab1:
    return 1;
}

static int r_K(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 3);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    do {
        if (z->c <= z->lb || z->p[z->c - 1] != 'l') goto lab0;
        z->c--;
        break;
    lab0:
        if (z->c <= z->lb || z->p[z->c - 1] != 'i') goto lab1;
        z->c--;
        break;
    lab1:
        if (z->c <= z->lb || z->p[z->c - 1] != 'e') return 0;
        z->c--;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 1);
            if (ret < 0) return 0;
            z->c = ret;
        }
        if (z->c <= z->lb || z->p[z->c - 1] != 'u') return 0;
        z->c--;
    } while (0);
    return 1;
}

static int r_L(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'u') goto lab0;
    z->c--;
    return 0;
lab0:
    if (z->c <= z->lb || z->p[z->c - 1] != 'x') goto lab1;
    z->c--;
    return 0;
lab1:
    {
        int v_2 = z->l - z->c;
        if (z->c <= z->lb || z->p[z->c - 1] != 's') goto lab2;
        z->c--;
        if (z->c <= z->lb || z->p[z->c - 1] != 'o') goto lab3;
        z->c--;
        goto lab2;
    lab3:
        return 0;
    lab2:
        z->c = z->l - v_2;
    }
    return 1;
}

static int r_M(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'a') goto lab0;
    z->c--;
    return 0;
lab0:
    if (z->c <= z->lb || z->p[z->c - 1] != 'c') goto lab1;
    z->c--;
    return 0;
lab1:
    if (z->c <= z->lb || z->p[z->c - 1] != 'e') goto lab2;
    z->c--;
    return 0;
lab2:
    if (z->c <= z->lb || z->p[z->c - 1] != 'm') goto lab3;
    z->c--;
    return 0;
lab3:
    return 1;
}

static int r_N(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 3);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    {
        int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
        if (ret < 0) return 0;
        z->c = ret;
    }
    do {
        int v_2 = z->l - z->c;
        if (z->c <= z->lb || z->p[z->c - 1] != 's') goto lab1;
        z->c--;
        goto lab0;
    lab1:
        break;
    lab0:
        z->c = z->l - v_2;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
    } while (0);
    return 1;
}

static int r_O(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    do {
        if (z->c <= z->lb || z->p[z->c - 1] != 'l') goto lab0;
        z->c--;
        break;
    lab0:
        if (z->c <= z->lb || z->p[z->c - 1] != 'i') return 0;
        z->c--;
    } while (0);
    return 1;
}

static int r_P(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'c') goto lab0;
    z->c--;
    return 0;
lab0:
    return 1;
}

static int r_Q(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    {
        int v_2 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 3);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_2;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'l') goto lab0;
    z->c--;
    return 0;
lab0:
    if (z->c <= z->lb || z->p[z->c - 1] != 'n') goto lab1;
    z->c--;
    return 0;
lab1:
    return 1;
}

static int r_R(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    do {
        if (z->c <= z->lb || z->p[z->c - 1] != 'n') goto lab0;
        z->c--;
        break;
    lab0:
        if (z->c <= z->lb || z->p[z->c - 1] != 'r') return 0;
        z->c--;
    } while (0);
    return 1;
}

static int r_S(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    do {
        if (!(eq_s_b(z, 2, s_1))) goto lab0;
        break;
    lab0:
        if (z->c <= z->lb || z->p[z->c - 1] != 't') return 0;
        z->c--;
        if (z->c <= z->lb || z->p[z->c - 1] != 't') goto lab1;
        z->c--;
        return 0;
    lab1:
        ;
    } while (0);
    return 1;
}

static int r_T(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    do {
        if (z->c <= z->lb || z->p[z->c - 1] != 's') goto lab0;
        z->c--;
        break;
    lab0:
        if (z->c <= z->lb || z->p[z->c - 1] != 't') return 0;
        z->c--;
        if (z->c <= z->lb || z->p[z->c - 1] != 'o') goto lab1;
        z->c--;
        return 0;
    lab1:
        ;
    } while (0);
    return 1;
}

static int r_U(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    do {
        if (z->c <= z->lb || z->p[z->c - 1] != 'l') goto lab0;
        z->c--;
        break;
    lab0:
        if (z->c <= z->lb || z->p[z->c - 1] != 'm') goto lab1;
        z->c--;
        break;
    lab1:
        if (z->c <= z->lb || z->p[z->c - 1] != 'n') goto lab2;
        z->c--;
        break;
    lab2:
        if (z->c <= z->lb || z->p[z->c - 1] != 'r') return 0;
        z->c--;
    } while (0);
    return 1;
}

static int r_V(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'c') return 0;
    z->c--;
    return 1;
}

static int r_W(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 's') goto lab0;
    z->c--;
    return 0;
lab0:
    if (z->c <= z->lb || z->p[z->c - 1] != 'u') goto lab1;
    z->c--;
    return 0;
lab1:
    return 1;
}

static int r_X(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    do {
        if (z->c <= z->lb || z->p[z->c - 1] != 'l') goto lab0;
        z->c--;
        break;
    lab0:
        if (z->c <= z->lb || z->p[z->c - 1] != 'i') goto lab1;
        z->c--;
        break;
    lab1:
        if (z->c <= z->lb || z->p[z->c - 1] != 'e') return 0;
        z->c--;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 1);
            if (ret < 0) return 0;
            z->c = ret;
        }
        if (z->c <= z->lb || z->p[z->c - 1] != 'u') return 0;
        z->c--;
    } while (0);
    return 1;
}

static int r_Y(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    return eq_s_b(z, 2, s_2);
}

static int r_Z(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'f') goto lab0;
    z->c--;
    return 0;
lab0:
    return 1;
}

static int r_AA(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] >> 5 != 3 || !((1839440 >> (z->p[z->c - 1] & 0x1f)) & 1)) return 0;
    return find_among_b(z, a_0, 9, 0) != 0;
}

static int r_BB(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 3);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (!(eq_s_b(z, 3, s_3))) goto lab0;
    return 0;
lab0:
    if (!(eq_s_b(z, 4, s_4))) goto lab1;
    return 0;
lab1:
    return 1;
}

static int r_CC(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        {
            int ret = skip_b_utf8(z->p, z->c, z->lb, 2);
            if (ret < 0) return 0;
            z->c = ret;
        }
        z->c = z->l - v_1;
    }
    if (z->c <= z->lb || z->p[z->c - 1] != 'l') return 0;
    z->c--;
    return 1;
}

static int r_endings(struct SN_env * z) {
    z->ket = z->c;
    if (!find_among_b(z, a_1, 294, af_1)) return 0;
    z->bra = z->c;
    {
        int ret = slice_del(z);
        if (ret < 0) return ret;
    }
    return 1;
}

static int r_undouble(struct SN_env * z) {
    {
        int v_1 = z->l - z->c;
        if (z->c - 1 <= z->lb || z->p[z->c - 1] >> 5 != 3 || !((1929364 >> (z->p[z->c - 1] & 0x1f)) & 1)) return 0;
        if (!find_among_b(z, a_2, 10, 0)) return 0;
        z->c = z->l - v_1;
    }
    z->ket = z->c;
    {
        int ret = skip_b_utf8(z->p, z->c, z->lb, 1);
        if (ret < 0) return 0;
        z->c = ret;
    }
    z->bra = z->c;
    {
        int ret = slice_del(z);
        if (ret < 0) return ret;
    }
    return 1;
}

static int r_respell(struct SN_env * z) {
    int among_var;
    z->ket = z->c;
    if (z->c - 1 <= z->lb || z->p[z->c - 1] >> 5 != 3 || !((89919504 >> (z->p[z->c - 1] & 0x1f)) & 1)) return 0;
    among_var = find_among_b(z, a_3, 34, 0);
    if (!among_var) return 0;
    z->bra = z->c;
    switch (among_var) {
        case 1:
            {
                int ret = slice_from_s(z, 3, s_5);
                if (ret < 0) return ret;
            }
            break;
        case 2:
            {
                int ret = slice_from_s(z, 2, s_6);
                if (ret < 0) return ret;
            }
            break;
        case 3:
            {
                int ret = slice_from_s(z, 2, s_7);
                if (ret < 0) return ret;
            }
            break;
        case 4:
            {
                int ret = slice_from_s(z, 2, s_8);
                if (ret < 0) return ret;
            }
            break;
        case 5:
            {
                int ret = slice_from_s(z, 2, s_9);
                if (ret < 0) return ret;
            }
            break;
        case 6:
            {
                int ret = slice_from_s(z, 5, s_10);
                if (ret < 0) return ret;
            }
            break;
        case 7:
            {
                int ret = slice_from_s(z, 5, s_11);
                if (ret < 0) return ret;
            }
            break;
        case 8:
            {
                int ret = slice_from_s(z, 4, s_12);
                if (ret < 0) return ret;
            }
            break;
        case 9:
            if (z->c <= z->lb || z->p[z->c - 1] != 'a') goto lab0;
            z->c--;
            return 0;
        lab0:
            if (z->c <= z->lb || z->p[z->c - 1] != 'i') goto lab1;
            z->c--;
            return 0;
        lab1:
            if (z->c <= z->lb || z->p[z->c - 1] != 'o') goto lab2;
            z->c--;
            return 0;
        lab2:
            {
                int ret = slice_from_s(z, 1, s_13);
                if (ret < 0) return ret;
            }
            break;
        case 10:
            {
                int ret = slice_from_s(z, 3, s_14);
                if (ret < 0) return ret;
            }
            break;
        case 11:
            {
                int ret = slice_from_s(z, 3, s_15);
                if (ret < 0) return ret;
            }
            break;
        case 12:
            {
                int ret = slice_from_s(z, 3, s_16);
                if (ret < 0) return ret;
            }
            break;
        case 13:
            {
                int ret = slice_from_s(z, 3, s_17);
                if (ret < 0) return ret;
            }
            break;
        case 14:
            {
                int ret = slice_from_s(z, 2, s_18);
                if (ret < 0) return ret;
            }
            break;
        case 15:
            {
                int ret = slice_from_s(z, 2, s_19);
                if (ret < 0) return ret;
            }
            break;
        case 16:
            {
                int ret = slice_from_s(z, 2, s_20);
                if (ret < 0) return ret;
            }
            break;
        case 17:
            {
                int ret = slice_from_s(z, 3, s_21);
                if (ret < 0) return ret;
            }
            break;
        case 18:
            {
                int ret = slice_from_s(z, 3, s_22);
                if (ret < 0) return ret;
            }
            break;
        case 19:
            {
                int ret = slice_from_s(z, 3, s_23);
                if (ret < 0) return ret;
            }
            break;
        case 20:
            {
                int ret = slice_from_s(z, 3, s_24);
                if (ret < 0) return ret;
            }
            break;
        case 21:
            {
                int ret = slice_from_s(z, 3, s_25);
                if (ret < 0) return ret;
            }
            break;
        case 22:
            {
                int ret = slice_from_s(z, 4, s_26);
                if (ret < 0) return ret;
            }
            break;
        case 23:
            {
                int ret = slice_from_s(z, 4, s_27);
                if (ret < 0) return ret;
            }
            break;
        case 24:
            if (z->c <= z->lb || z->p[z->c - 1] != 's') goto lab3;
            z->c--;
            return 0;
        lab3:
            {
                int ret = slice_from_s(z, 3, s_28);
                if (ret < 0) return ret;
            }
            break;
        case 25:
            {
                int ret = slice_from_s(z, 3, s_29);
                if (ret < 0) return ret;
            }
            break;
        case 26:
            {
                int ret = slice_from_s(z, 3, s_30);
                if (ret < 0) return ret;
            }
            break;
        case 27:
            {
                int ret = slice_from_s(z, 3, s_31);
                if (ret < 0) return ret;
            }
            break;
        case 28:
            if (z->c <= z->lb || z->p[z->c - 1] != 'p') goto lab4;
            z->c--;
            return 0;
        lab4:
            if (z->c <= z->lb || z->p[z->c - 1] != 't') goto lab5;
            z->c--;
            return 0;
        lab5:
            {
                int ret = slice_from_s(z, 3, s_32);
                if (ret < 0) return ret;
            }
            break;
        case 29:
            {
                int ret = slice_from_s(z, 3, s_33);
                if (ret < 0) return ret;
            }
            break;
        case 30:
            if (z->c <= z->lb || z->p[z->c - 1] != 'm') goto lab6;
            z->c--;
            return 0;
        lab6:
            {
                int ret = slice_from_s(z, 3, s_34);
                if (ret < 0) return ret;
            }
            break;
        case 31:
            {
                int ret = slice_from_s(z, 3, s_35);
                if (ret < 0) return ret;
            }
            break;
        case 32:
            if (z->c <= z->lb || z->p[z->c - 1] != 'n') goto lab7;
            z->c--;
            return 0;
        lab7:
            {
                int ret = slice_from_s(z, 2, s_36);
                if (ret < 0) return ret;
            }
            break;
        case 33:
            {
                int ret = slice_from_s(z, 2, s_37);
                if (ret < 0) return ret;
            }
            break;
    }
    return 1;
}

extern int lovins_UTF_8_stem(struct SN_env * z) {
    z->lb = z->c; z->c = z->l;
    {
        int v_1 = z->l - z->c;
        {
            int ret = r_endings(z);
            if (ret < 0) return ret;
        }
        z->c = z->l - v_1;
    }
    {
        int v_2 = z->l - z->c;
        {
            int ret = r_undouble(z);
            if (ret < 0) return ret;
        }
        z->c = z->l - v_2;
    }
    {
        int v_3 = z->l - z->c;
        {
            int ret = r_respell(z);
            if (ret < 0) return ret;
        }
        z->c = z->l - v_3;
    }
    z->c = z->lb;
    return 1;
}

extern struct SN_env * lovins_UTF_8_create_env(void) {
    return SN_new_env(sizeof(struct SN_env));
}

extern void lovins_UTF_8_close_env(struct SN_env * z) {
    SN_delete_env(z);
}

