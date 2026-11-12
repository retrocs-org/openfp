/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include "common/operation.h"
static Game g;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void)
{
    MatchSetup crew={0}; DesertOperation op; InputBits in[MAX_TANKS]={0}; fx x,y; int k;
    crew.players=1;
    CHECK(operation_challenge_start(&g,&op,&crew,CHALLENGE_RESCUE)==0);
    CHECK(g.biome==BIOME_JUNGLE && g.time_limit==180*TICK_RATE && g.num_tanks==1);
    CHECK(operation_target(&g,&op,0,&x,&y));
    g.map.st[op.gun].hp=0; g.tanks[0].saved=4; operation_step(&g,&op);
    CHECK(op.phase==OP_WON && g.winner==TEAM_PLAYER);
    CHECK(operation_challenge_start(&g,&op,&crew,CHALLENGE_BRIDGE)==0);
    CHECK(g.biome==BIOME_DESERT && g.num_cp==1 && g.time_limit==120*TICK_RATE);
    g.tanks[0].x=g.tanks[0].prev_x=FX(48*16+8); g.tanks[0].y=g.tanks[0].prev_y=FX(35*16+8);
    g.tanks[0].respawn=0;
    for(k=0;k<CP_TAKE+5 && op.phase<OP_WON;k++){game_tick(&g,in);operation_step(&g,&op);}
    CHECK(op.phase==OP_WON && g.cp_owner[0]==TEAM_PLAYER);
    CHECK(operation_challenge_start(&g,&op,&crew,CHALLENGE_RAID)==0);
    CHECK(g.biome==BIOME_VOLCANIC && g.tanks[0].lives==1 && op.gun_count==3);
    for(k=0;k<3;k++)g.map.st[op.guns[k]].hp=0;
    operation_step(&g,&op); CHECK(op.phase==OP_WON && op.destroyed==3);
    CHECK(operation_challenge_start(&g,&op,&crew,CHALLENGE_RAID)==0);
    g.tanks[0].out=1; operation_step(&g,&op); CHECK(op.phase==OP_LOST);
    CHECK(operation_challenge_start(&g,&op,&crew,CHALLENGE_RESCUE)==0);
    g.time_up=1; operation_step(&g,&op); CHECK(op.phase==OP_LOST);
    puts("three field challenges: objectives, capture, loss and limits ok"); return 0;
}
