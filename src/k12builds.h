/* k12builds.h - generated (tools/k12map): every NBA 2K12 address BodyHook uses, per exe build.
 * Build A (RLD 1.0x patch) is the one the port was made on; B and C were mapped from it by masked code
 * signatures, code references and table contents, and every value is checked again before patching. */
typedef struct {
    u32 ts; const char *name;
    u32 resolve;
    u32 helper;
    u32 helper_in;
    u32 cf_slot5;
    u32 get_j_a;
    u32 get_j_b;
    u32 get_s_a;
    u32 get_s_b;
    u32 sim_j;
    u32 sim_jp;
    u32 sim_s;
    u32 sim_sp;
    u32 acc_builder;
    u32 acc_dtor;
    u32 sock_pick;
    u32 team1;
    u32 team2;
    u32 floor1;
    u32 floor8;
    u32 floor9;
    u32 floor10;
    u32 floor11;
    u32 comp_ctor;
    u32 bench_gate;
    u32 body_site;
    u32 cl_ja;
    u32 cl_jb;
    u32 cl_sa;
    u32 cl_sb;
    u32 cache0;
    u32 cache1;
    u32 cache2;
    u32 cache3;
    u32 fetch_j;
    u32 fetch_jp;
    u32 fetch_s;
    u32 fetch_sp;
    u32 acc_tex;
    u32 acc_nrm;
    u32 sock_col;
    u32 sock_t1;
    u32 sock_t2;
    u32 bench_sb;
    u32 bench_sa;
    u32 bench_off;
    u32 mgr;
    u32 arr_j;
    u32 arr_s;
    u32 cfvt0;
    u32 cfvt1;
    u32 cfvt2;
    u32 cfvt3;
    u32 acc_table;
    u32 acc_vt0;
    u32 sock_vt;
    u32 lin;
    u32 bench_vt;
    u32 floor_vt;
    u32 cloth_dt;
    u32 detail;
    u32 bodyids;
} K12Build;

static const K12Build g_k12Builds[3] = {
  { 0x4F219A21u, "NBA 2K12 1.0x patch (RLD)",
    0x00C864E0u, 0x00B6E080u, 0x00B6EC70u, 0x00B6E050u, 0x00B69110u, 0x00B69210u,
    0x00B69150u, 0x00B69250u, 0x00B694A0u, 0x00B694F0u, 0x00B69330u, 0x00B69380u,
    0x00B6D840u, 0x00B6D680u, 0x00B6CB60u, 0x00C05D30u, 0x00C05D90u, 0x00B7B8C0u,
    0x00B7A9B0u, 0x00B7B020u, 0x00B7B220u, 0x00B7B420u, 0x00BCC9C0u, 0x00BCC750u,
    0x00B7A1C8u, 0x00B7A537u, 0x00B7A55Eu, 0x00B7A524u, 0x00B7A54Bu, 0x00B68D59u,
    0x00B68E7Du, 0x00B68F7Bu, 0x00B69073u, 0x00B694D2u, 0x00B69538u, 0x00B69362u,
    0x00B693C2u, 0x00B6D906u, 0x00B6D925u, 0x00B6DAF0u, 0x00B6D6B6u, 0x00B6D786u,
    0x00BCCD08u, 0x00BCCC6Bu, 0x00BCC9EFu, 0x01D40C70u, 0x01D2422Cu, 0x01D243ACu,
    0x00DD7210u, 0x00DDBB90u, 0x00E5D7E8u, 0x00E69224u, 0x0124C54Cu, 0x00E5FC24u,
    0x00E5FCA4u, 0x00E74860u, 0x00E6260Cu, 0x00E62674u, 0x01A22070u, 0x013B8AA4u,
    0x00E602F4u,
  },
  { 0x4E63057Du, "NBA 2K12 1.0 original (e.g. \"NBA 2K12/2K25 REMASTERED.exe\", nba2k13.exe)",
    0x00C68690u, 0x00B18740u, 0x00B19480u, 0x00B18660u, 0x00B13730u, 0x00B13830u,
    0x00B13770u, 0x00B13870u, 0x00B13AD0u, 0x00B13B20u, 0x00B13950u, 0x00B139A0u,
    0x00B17E70u, 0x00B17CB0u, 0x00B17190u, 0x00BAFD30u, 0x00BAFD90u, 0x00B25B50u,
    0x00B24C40u, 0x00B252B0u, 0x00B254B0u, 0x00B256B0u, 0x00B77150u, 0x00B76EF0u,
    0x00B244C8u, 0x00B24837u, 0x00B2485Eu, 0x00B24824u, 0x00B2484Bu, 0x00B13379u,
    0x00B1349Du, 0x00B1359Bu, 0x00B13693u, 0x00B13B02u, 0x00B13B68u, 0x00B13982u,
    0x00B139E2u, 0x00B17F2Du, 0x00B17F4Cu, 0x00B18110u, 0x00B17CE6u, 0x00B17DB6u,
    0x00B77498u, 0x00B773FBu, 0x00B7717Fu, 0x01CA2590u, 0x01C866DCu, 0x01C8685Cu,
    0x00DB5790u, 0x00DB8C90u, 0x00E3A308u, 0x00E458B4u, 0x011BFB8Cu, 0x00E3C554u,
    0x00E3C5D4u, 0x00E507E0u, 0x00E3EEA4u, 0x00E3EF0Cu, 0x01984F10u, 0x013222E4u,
    0x00E3CBD4u,
  },
  { 0x4E84D35Fu, "NBA 2K12 1.0 SecuROM retail (4E84D35F)",
    0x00C68900u, 0x00B18690u, 0x00B193D0u, 0x00B185B0u, 0x00B13680u, 0x00B13780u,
    0x00B136C0u, 0x00B137C0u, 0x00B13A20u, 0x00B13A70u, 0x00B138A0u, 0x00B138F0u,
    0x00B17DC0u, 0x00B17C00u, 0x00B170E0u, 0x00BAFD70u, 0x00BAFDD0u, 0x00B25AB0u,
    0x00B24BA0u, 0x00B25210u, 0x00B25410u, 0x00B25610u, 0x00B77100u, 0x00B76EA0u,
    0x00B24428u, 0x00B24797u, 0x00B247BEu, 0x00B24784u, 0x00B247ABu, 0x00B132C9u,
    0x00B133EDu, 0x00B134EBu, 0x00B135E3u, 0x00B13A52u, 0x00B13AB8u, 0x00B138D2u,
    0x00B13932u, 0x00B17E7Du, 0x00B17E9Cu, 0x00B18060u, 0x00B17C36u, 0x00B17D06u,
    0x00B77448u, 0x00B773ABu, 0x00B7712Fu, 0x01CA2590u, 0x01C866DCu, 0x01C8685Cu,
    0x00DB57E0u, 0x00DB8CD0u, 0x00E3A338u, 0x00E45914u, 0x011BFB8Cu, 0x00E3C594u,
    0x00E3C614u, 0x00E50820u, 0x00E3EF04u, 0x00E3EF6Cu, 0x01984F10u, 0x013222E4u,
    0x00E3CC04u,
  },
};
static const K12Build *g_k12 = &g_k12Builds[0];
