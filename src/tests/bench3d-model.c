#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../apps/bench3d/model.h"
int main(void){
    assert(bench_sine(0)==0 && bench_sine(256)==32768 && bench_sine(512)==0 && bench_sine(768)==-32768);
    struct bench_face faces[BENCH3D_MAX_CUBES*6],reference[BENCH3D_MAX_CUBES*6];
    const unsigned counts[]={1,4,17,64},sizes[][2]={{320,128},{1024,660},{32767,32767}};
    unsigned scenes=0;
    for(unsigned c=0;c<4;++c)for(unsigned s=0;s<3;++s)for(unsigned angle=0;angle<5120;angle+=37){
        unsigned n=bench_scene(faces,counts[c],angle,sizes[s][0],sizes[s][1]);assert(n && n<=counts[c]*3);
        for(unsigned i=0;i<n;++i){assert(faces[i].depth>0 && (faces[i].color>>24)==255);if(i)assert(faces[i-1].depth>=faces[i].depth);
            for(unsigned p=0;p<4;++p)assert(faces[i].p[p].x>=0 && (unsigned)faces[i].p[p].x<sizes[s][0] && faces[i].p[p].y>=0 && (unsigned)faces[i].p[p].y<sizes[s][1]);
        }
        unsigned m=bench_scene(reference,counts[c],angle+5120,sizes[s][0],sizes[s][1]);assert(m==n && !memcmp(reference,faces,n*sizeof(*faces)));++scenes;
    }
    assert(!bench_scene(faces,0,0,1024,660) && !bench_scene(faces,65,0,1024,660) && !bench_scene(faces,1,0,0,660));
    assert(bench_scene(faces,1,UINT32_MAX,1024,660));
    printf("PASS bench3d: %u scenes, winding/culling, depth ordering, bounded projection, periodic rotation and max workload without overflow\n",scenes);
}
