
#define TESTDATALEN 512
const unsigned int testdata[TESTDATALEN]=
{
  0x4F432041, //[0x00000]
  0x43454E4E, //[0x00001]
  0x55434954, //[0x00002]
  0x41592054, //[0x00003]
  0x45454B4E, //[0x00004]
  0x204E4920, //[0x00005]
  0x474E494B, //[0x00006]
  0x54524120, //[0x00007]
  0x27525548, //[0x00008]
  0x4F432053, //[0x00009]
  0x0D545255, //[0x0000A]
  0x620A0D0A, //[0x0000B]
  0x0D0A0D79, //[0x0000C]
  0x52414D0A, //[0x0000D]
  0x5754204B, //[0x0000E]
  0x0D4E4941, //[0x0000F]
  0x6153280A, //[0x00010]
  0x6C65756D, //[0x00011]
  0x202E4C20, //[0x00012]
  0x6D656C43, //[0x00013]
  0x29736E65, //[0x00014]
  0x0A0D0A0D, //[0x00015]
  0x0A0D0A0D, //[0x00016]
  0x52500A0D, //[0x00017]
  0x43414645, //[0x00018]
  0x0D0A0D45, //[0x00019]
  0x6568540A, //[0x0001A]
  0x676E7520, //[0x0001B]
  0x6C746E65, //[0x0001C]
  0x616C2065, //[0x0001D]
  0x61207377, //[0x0001E]
  0x6320646E, //[0x0001F]
  0x6F747375, //[0x00020]
  0x7420736D, //[0x00021]
  0x6863756F, //[0x00022]
  0x75206465, //[0x00023]
  0x206E6F70, //[0x00024]
  0x74206E69, //[0x00025]
  0x20736968, //[0x00026]
  0x656C6174, //[0x00027]
  0x65726120, //[0x00028]
  0x69680A0D, //[0x00029]
  0x726F7473, //[0x0002A]
  0x6C616369, //[0x0002B]
  0x6E61202C, //[0x0002C]
  0x68742064, //[0x0002D]
  0x70652065, //[0x0002E]
  0x646F7369, //[0x0002F]
  0x77207365, //[0x00030]
  0x68636968, //[0x00031]
  0x65726120, //[0x00032]
  0x65737520, //[0x00033]
  0x6F742064, //[0x00034]
  0x6C6C6920, //[0x00035]
  0x72747375, //[0x00036]
  0x20657461, //[0x00037]
  0x6D656874, //[0x00038]
  0x72610A0D, //[0x00039]
  0x6C612065, //[0x0003A]
  0x68206F73, //[0x0003B]
  0x6F747369, //[0x0003C]
  0x61636972, //[0x0003D]
  0x20202E6C, //[0x0003E]
  0x69207449, //[0x0003F]
  0x6F6E2073, //[0x00040]
  0x72702074, //[0x00041]
  0x6E657465, //[0x00042]
  0x20646564, //[0x00043]
  0x74616874, //[0x00044]
  0x65687420, //[0x00045]
  0x6C206573, //[0x00046]
  0x20737761, //[0x00047]
  0x0D646E61, //[0x00048]
  0x7375630A, //[0x00049]
  0x736D6F74, //[0x0004A]
  0x69786520, //[0x0004B]
  0x64657473, //[0x0004C]
  0x206E6920, //[0x0004D]
  0x6C676E45, //[0x0004E]
  0x20646E61, //[0x0004F]
  0x74206E69, //[0x00050]
  0x73206568, //[0x00051]
  0x68747869, //[0x00052]
  0x6E656320, //[0x00053]
  0x79727574, //[0x00054]
  0x6F6E203B, //[0x00055]
  0x7469202C, //[0x00056]
  0x20736920, //[0x00057]
  0x796C6E6F, //[0x00058]
  0x72700A0D, //[0x00059]
  0x6E657465, //[0x0005A]
  0x20646564, //[0x0005B]
  0x74616874, //[0x0005C]
  0x616E6920, //[0x0005D]
  0x63756D73, //[0x0005E]
  0x73612068, //[0x0005F]
  0x65687420, //[0x00060]
  0x78652079, //[0x00061]
  0x65747369, //[0x00062]
  0x6E692064, //[0x00063]
  0x65687420, //[0x00064]
  0x676E4520, //[0x00065]
  0x6873696C, //[0x00066]
  0x646E6120, //[0x00067]
  0x68746F20, //[0x00068]
  0x0A0D7265, //[0x00069]
  0x69766963, //[0x0006A]
  0x617A696C, //[0x0006B]
  0x6E6F6974, //[0x0006C]
  0x666F2073, //[0x0006D]
  0x72616620, //[0x0006E]
  0x74616C20, //[0x0006F]
  0x74207265, //[0x00070]
  0x73656D69, //[0x00071]
  0x7469202C, //[0x00072]
  0x20736920, //[0x00073]
  0x65666173, //[0x00074]
  0x206F7420, //[0x00075]
  0x736E6F63, //[0x00076]
  0x72656469, //[0x00077]
  0x61687420, //[0x00078]
  0x74692074, //[0x00079]
  0x0D736920, //[0x0007A]
  0x206F6E0A, //[0x0007B]
  0x6562696C, //[0x0007C]
  0x7075206C, //[0x0007D]
  0x74206E6F, //[0x0007E]
  0x73206568, //[0x0007F]
  0x68747869, //[0x00080]
  0x6E656320, //[0x00081]
  0x79727574, //[0x00082]
  0x206F7420, //[0x00083]
  0x70707573, //[0x00084]
  0x2065736F, //[0x00085]
  0x6D656874, //[0x00086]
  0x206F7420, //[0x00087]
  0x65766168, //[0x00088]
  0x65656220, //[0x00089]
  0x6E69206E, //[0x0008A]
  0x72700A0D, //[0x0008B]
  0x69746361, //[0x0008C]
  0x69206563, //[0x0008D]
  0x6874206E, //[0x0008E]
  0x64207461, //[0x0008F]
  0x61207961, //[0x00090]
  0x2E6F736C, //[0x00091]
  0x6E4F2020, //[0x00092]
  0x73692065, //[0x00093]
  0x69757120, //[0x00094]
  0x6A206574, //[0x00095]
  0x69747375, //[0x00096]
  0x64656966, //[0x00097]
  0x206E6920, //[0x00098]
  0x65666E69, //[0x00099]
  0x6E697272, //[0x0009A]
  0x740A0D67, //[0x0009B]
  0x20746168, //[0x0009C]
  0x74616877, //[0x0009D]
  0x72657665, //[0x0009E]
  0x656E6F20, //[0x0009F]
  0x20666F20, //[0x000A0]
  0x73656874, //[0x000A1]
  0x616C2065, //[0x000A2]
  0x6F207377, //[0x000A3]
  0x75632072, //[0x000A4]
  0x6D6F7473, //[0x000A5]
  0x61772073, //[0x000A6]
  0x616C2073, //[0x000A7]
  0x6E696B63, //[0x000A8]
  0x6E692067, //[0x000A9]
  0x61687420, //[0x000AA]
  0x720A0D74, //[0x000AB]
  0x746F6D65, //[0x000AC]
  0x69742065, //[0x000AD]
  0x202C656D, //[0x000AE]
  0x20737469, //[0x000AF]
  0x63616C70, //[0x000B0]
  0x61772065, //[0x000B1]
  0x6F632073, //[0x000B2]
  0x7465706D, //[0x000B3]
  0x6C746E65, //[0x000B4]
  0x69662079, //[0x000B5]
  0x64656C6C, //[0x000B6]
  0x20796220, //[0x000B7]
  0x6F772061, //[0x000B8]
  0x20657372, //[0x000B9]
  0x2E656E6F, //[0x000BA]
  0x0A0D0A0D, //[0x000BB]
  0x20656854, //[0x000BC]
  0x73657571, //[0x000BD]
  0x6E6F6974, //[0x000BE]
  0x20736120, //[0x000BF]
  0x77206F74, //[0x000C0]
  0x68746568, //[0x000C1]
  0x74207265, //[0x000C2]
  0x65726568, //[0x000C3]
  0x20736920, //[0x000C4]
  0x68637573, //[0x000C5]
  0x74206120, //[0x000C6]
  0x676E6968, //[0x000C7]
  0x20736120, //[0x000C8]
  0x69766964, //[0x000C9]
  0x7220656E, //[0x000CA]
  0x74686769, //[0x000CB]
  0x666F0A0D, //[0x000CC]
  0x6E696B20, //[0x000CD]
  0x69207367, //[0x000CE]
  0x6F6E2073, //[0x000CF]
  0x65732074, //[0x000D0]
  0x656C7474, //[0x000D1]
  0x6E692064, //[0x000D2]
  0x69687420, //[0x000D3]
  0x6F622073, //[0x000D4]
  0x202E6B6F, //[0x000D5]
  0x20744920, //[0x000D6]
  0x20736177, //[0x000D7]
  0x6E756F66, //[0x000D8]
  0x6F742064, //[0x000D9]
  0x6964206F, //[0x000DA]
  0x63696666, //[0x000DB]
  0x2E746C75, //[0x000DC]
  0x68540A0D, //[0x000DD]
  0x74207461, //[0x000DE]
  0x65206568, //[0x000DF]
  0x75636578, //[0x000E0]
  0x65766974, //[0x000E1]
  0x61656820, //[0x000E2]
  0x666F2064, //[0x000E3]
  0x6E206120, //[0x000E4]
  0x6F697461, //[0x000E5]
  0x6873206E, //[0x000E6]
  0x646C756F, //[0x000E7]
  0x20656220, //[0x000E8]
  0x65702061, //[0x000E9]
  0x6E6F7372, //[0x000EA]
  0x20666F20, //[0x000EB]
  0x74666F6C, //[0x000EC]
  0x630A0D79, //[0x000ED]
  0x61726168, //[0x000EE]
  0x72657463, //[0x000EF]
  0x646E6120, //[0x000F0]
  0x74786520, //[0x000F1]
  0x726F6172, //[0x000F2]
  0x616E6964, //[0x000F3]
  0x61207972, //[0x000F4]
  0x696C6962, //[0x000F5]
  0x202C7974, //[0x000F6]
  0x20736177, //[0x000F7]
  0x696E616D, //[0x000F8]
  0x74736566, //[0x000F9]
  0x646E6120, //[0x000FA]
  0x646E6920, //[0x000FB]
  0x75707369, //[0x000FC]
  0x6C626174, //[0x000FD]
  0x0A0D3B65, //[0x000FE]
  0x74616874, //[0x000FF]
  0x6E6F6E20, //[0x00100]
  0x75622065, //[0x00101]
  0x68742074, //[0x00102]
  0x65442065, //[0x00103]
  0x20797469, //[0x00104]
  0x6C756F63, //[0x00105]
  0x65732064, //[0x00106]
  0x7463656C, //[0x00107]
  0x61687420, //[0x00108]
  0x65682074, //[0x00109]
  0x75206461, //[0x0010A]
  0x7272656E, //[0x0010B]
  0x6C676E69, //[0x0010C]
  0x77202C79, //[0x0010D]
  0x0A0D7361, //[0x0010E]
  0x6F736C61, //[0x0010F]
  0x6E616D20, //[0x00110]
  0x73656669, //[0x00111]
  0x6E612074, //[0x00112]
  0x6E692064, //[0x00113]
  0x70736964, //[0x00114]
  0x62617475, //[0x00115]
  0x203B656C, //[0x00116]
  0x74616874, //[0x00117]
  0x65687420, //[0x00118]
  0x69654420, //[0x00119]
  0x6F207974, //[0x0011A]
  0x74686775, //[0x0011B]
  0x206F7420, //[0x0011C]
  0x656B616D, //[0x0011D]
  0x61687420, //[0x0011E]
  0x730A0D74, //[0x0011F]
  0x63656C65, //[0x00120]
  0x6E6F6974, //[0x00121]
  0x6874202C, //[0x00122]
  0x202C6E65, //[0x00123]
  0x20736177, //[0x00124]
  0x656B696C, //[0x00125]
  0x65736977, //[0x00126]
  0x6E616D20, //[0x00127]
  0x73656669, //[0x00128]
  0x6E612074, //[0x00129]
  0x6E692064, //[0x0012A]
  0x70736964, //[0x0012B]
  0x62617475, //[0x0012C]
  0x203B656C, //[0x0012D]
  0x736E6F63, //[0x0012E]
  0x65757165, //[0x0012F]
  0x796C746E, //[0x00130]
  0x740A0D2C, //[0x00131]
  0x20746168, //[0x00132]
  0x64206548, //[0x00133]
  0x2073656F, //[0x00134]
  0x656B616D, //[0x00135]
  0x2C746920, //[0x00136]
  0x20736120, //[0x00137]
  0x69616C63, //[0x00138]
  0x2C64656D, //[0x00139]
  0x73617720, //[0x0013A]
  0x206E6120, //[0x0013B]
  0x76616E75, //[0x0013C]
  0x6164696F, //[0x0013D]
  0x20656C62, //[0x0013E]
  0x75646564, //[0x0013F]
  0x6F697463, //[0x00140]
  0x0A0D2E6E, //[0x00141]
  0x656D2049, //[0x00142]
  0x202C6E61, //[0x00143]
  0x69746E75, //[0x00144]
  0x6874206C, //[0x00145]
  0x75612065, //[0x00146]
  0x726F6874, //[0x00147]
  0x20666F20, //[0x00148]
  0x73696874, //[0x00149]
  0x6F6F6220, //[0x0014A]
  0x6E65206B, //[0x0014B]
  0x6E756F63, //[0x0014C]
  0x65726574, //[0x0014D]
  0x68742064, //[0x0014E]
  0x6F502065, //[0x0014F]
  0x6461706D, //[0x00150]
  0x2C72756F, //[0x00151]
  0x6E610A0D, //[0x00152]
  0x614C2064, //[0x00153]
  0x43207964, //[0x00154]
  0x6C747361, //[0x00155]
  0x69616D65, //[0x00156]
  0x202C656E, //[0x00157]
  0x20646E61, //[0x00158]
  0x656D6F73, //[0x00159]
  0x68746F20, //[0x0015A]
  0x65207265, //[0x0015B]
  0x75636578, //[0x0015C]
  0x65766974, //[0x0015D]
  0x61656820, //[0x0015E]
  0x6F207364, //[0x0015F]
  0x68742066, //[0x00160]
  0x6B207461, //[0x00161]
  0x3B646E69, //[0x00162]
  0x68740A0D, //[0x00163]
  0x20657365, //[0x00164]
  0x65726577, //[0x00165]
  0x756F6620, //[0x00166]
  0x7320646E, //[0x00167]
  0x6964206F, //[0x00168]
  0x63696666, //[0x00169]
  0x20746C75, //[0x0016A]
  0x77206F74, //[0x0016B]
  0x206B726F, //[0x0016C]
  0x6F746E69, //[0x0016D]
  0x65687420, //[0x0016E]
  0x68637320, //[0x0016F]
  0x2C656D65, //[0x00170]
  0x61687420, //[0x00171]
  0x74692074, //[0x00172]
  0x61770A0D, //[0x00173]
  0x756A2073, //[0x00174]
  0x64656764, //[0x00175]
  0x74656220, //[0x00176]
  0x20726574, //[0x00177]
  0x74206F74, //[0x00178]
  0x20656B61, //[0x00179]
  0x20656874, //[0x0017A]
  0x6568746F, //[0x0017B]
  0x61742072, //[0x0017C]
  0x69206B63, //[0x0017D]
  0x6874206E, //[0x0017E]
  0x62207369, //[0x0017F]
  0x206B6F6F, //[0x00180]
  0x69687728, //[0x00181]
  0x0A0D6863, //[0x00182]
  0x7473756D, //[0x00183]
  0x20656220, //[0x00184]
  0x75737369, //[0x00185]
  0x74206465, //[0x00186]
  0x20736968, //[0x00187]
  0x6C6C6166, //[0x00188]
  0x61202C29, //[0x00189]
  0x7420646E, //[0x0018A]
  0x206E6568, //[0x0018B]
  0x69206F67, //[0x0018C]
  0x206F746E, //[0x0018D]
  0x69617274, //[0x0018E]
  0x676E696E, //[0x0018F]
  0x646E6120, //[0x00190]
  0x74657320, //[0x00191]
  0x0D656C74, //[0x00192]
  0x6568740A, //[0x00193]
  0x65757120, //[0x00194]
  0x6F697473, //[0x00195]
  0x6E69206E, //[0x00196]
  0x6F6E6120, //[0x00197]
  0x72656874, //[0x00198]
  0x6F6F6220, //[0x00199]
  0x20202E6B, //[0x0019A]
  0x69207449, //[0x0019B]
  0x6F202C73, //[0x0019C]
  0x6F632066, //[0x0019D]
  0x65737275, //[0x0019E]
  0x2061202C, //[0x0019F]
  0x6E696874, //[0x001A0]
  0x68772067, //[0x001A1]
  0x0D686369, //[0x001A2]
  0x67756F0A, //[0x001A3]
  0x74207468, //[0x001A4]
  0x6562206F, //[0x001A5]
  0x74657320, //[0x001A6]
  0x64656C74, //[0x001A7]
  0x6E61202C, //[0x001A8]
  0x20492064, //[0x001A9]
  0x6E206D61, //[0x001AA]
  0x6720746F, //[0x001AB]
  0x676E696F, //[0x001AC]
  0x206F7420, //[0x001AD]
  0x65766168, //[0x001AE]
  0x796E6120, //[0x001AF]
  0x6E696874, //[0x001B0]
  0x61702067, //[0x001B1]
  0x63697472, //[0x001B2]
  0x72616C75, //[0x001B3]
  0x6F740A0D, //[0x001B4]
  0x206F6420, //[0x001B5]
  0x7478656E, //[0x001B6]
  0x6E697720, //[0x001B7]
  0x20726574, //[0x001B8]
  0x77796E61, //[0x001B9]
  0x0D2E7961, //[0x001BA]
  0x4D0A0D0A, //[0x001BB]
  0x204B5241, //[0x001BC]
  0x49415754, //[0x001BD]
  0x0D0A0D4E, //[0x001BE]
  0x5241480A, //[0x001BF]
  0x524F4654, //[0x001C0]
  0x4A202C44, //[0x001C1]
  0x20796C75, //[0x001C2]
  0x202C3132, //[0x001C3]
  0x39383831, //[0x001C4]
  0x0A0D0A0D, //[0x001C5]
  0x0A0D0A0D, //[0x001C6]
  0x0A0D0A0D, //[0x001C7]
  0x20410A0D, //[0x001C8]
  0x4E4E4F43, //[0x001C9]
  0x49544345, //[0x001CA]
  0x20545543, //[0x001CB]
  0x4B4E4159, //[0x001CC]
  0x49204545, //[0x001CD]
  0x494B204E, //[0x001CE]
  0x4120474E, //[0x001CF]
  0x55485452, //[0x001D0]
  0x20532752, //[0x001D1]
  0x52554F43, //[0x001D2]
  0x0D0A0D54, //[0x001D3]
  0x0D0A0D0A, //[0x001D4]
  0x410A0D0A, //[0x001D5]
  0x524F5720, //[0x001D6]
  0x464F2044, //[0x001D7]
  0x50584520, //[0x001D8]
  0x414E414C, //[0x001D9]
  0x4E4F4954, //[0x001DA]
  0x0A0D0A0D, //[0x001DB]
  0x77207449, //[0x001DC]
  0x69207361, //[0x001DD]
  0x6157206E, //[0x001DE]
  0x63697772, //[0x001DF]
  0x6143206B, //[0x001E0]
  0x656C7473, //[0x001E1]
  0x61687420, //[0x001E2]
  0x20492074, //[0x001E3]
  0x656D6163, //[0x001E4]
  0x72636120, //[0x001E5]
  0x2073736F, //[0x001E6]
  0x20656874, //[0x001E7]
  0x69727563, //[0x001E8]
  0x2073756F, //[0x001E9]
  0x61727473, //[0x001EA]
  0x7265676E, //[0x001EB]
  0x68770A0D, //[0x001EC]
  0x49206D6F, //[0x001ED]
  0x206D6120, //[0x001EE]
  0x6E696F67, //[0x001EF]
  0x6F742067, //[0x001F0]
  0x6C617420, //[0x001F1]
  0x6261206B, //[0x001F2]
  0x2E74756F, //[0x001F3]
  0x65482020, //[0x001F4]
  0x74746120, //[0x001F5]
  0x74636172, //[0x001F6]
  0x6D206465, //[0x001F7]
  0x79622065, //[0x001F8]
  0x72687420, //[0x001F9]
  0x74206565, //[0x001FA]
  0x676E6968, //[0x001FB]
  0x0A0D3A73, //[0x001FC]
  0x20736968, //[0x001FD]
  0x646E6163, //[0x001FE]
  0x73206469, //[0x001FF]
};

