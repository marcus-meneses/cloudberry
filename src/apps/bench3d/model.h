#ifndef CLOUDBERRY_BENCH3D_MODEL_H
#define CLOUDBERRY_BENCH3D_MODEL_H
#include <stdint.h>
#define BENCH3D_MAX_CUBES 64u
struct bench_point {int32_t x,y;};
struct bench_face {struct bench_point p[4];int32_t depth;uint32_t color;};
/* Fixed-point camera transforms; positive depth is away from the camera. */
static inline int32_t bench_sine(unsigned angle){
    unsigned a=angle&1023u;int sign=a<512?1:-1;int64_t x=a&511u,p=x*(512-x);
    return sign*(int32_t)(16*p*32768/(5*INT64_C(512)*512-4*p));
}
static inline unsigned bench_scene(struct bench_face *out,unsigned cubes,unsigned angle,uint32_t width,uint32_t height){
    if(!out || !cubes || cubes>BENCH3D_MAX_CUBES || width<64 || height<64)return 0;
    angle%=5120;
    static const int8_t vertices[8][3]={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    static const unsigned faces[6][4]={{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{0,1,5,4},{3,7,6,2}};
    static const uint32_t colors[6]={0xffe49b28,0xffe76c38,0xff427db2,0xff4db8ab,0xff9b74c5,0xffe6c85a};
    unsigned cols=1;while(cols*cols<cubes)++cols;unsigned rows=(cubes+cols-1)/cols,count=0;
    int32_t cw=width/cols,ch=height/rows,scale=cw<ch?cw:ch;
    for(unsigned cube=0;cube<cubes;++cube){
        unsigned a=angle+cube*73,b=angle*3/5+cube*47;int32_t sy=bench_sine(a),cy=bench_sine(a+256),sx=bench_sine(b),cx=bench_sine(b+256);
        struct bench_point projected[8];int32_t depth[8];
        for(unsigned i=0;i<8;++i){int32_t x=vertices[i][0]*1024,y=vertices[i][1]*1024,z=vertices[i][2]*1024;
            int32_t rx=(x*cy+z*sy)/32768,rz=(-x*sy+z*cy)/32768,ry=(y*cx-rz*sx)/32768;
            depth[i]=7000+(y*sx+rz*cx)/32768;
            projected[i]=(struct bench_point){(int32_t)(cube%cols)*cw+cw/2+rx*scale/depth[i],(int32_t)(cube/cols)*ch+ch/2-ry*scale/depth[i]};
        }
        for(unsigned f=0;f<6;++f){const unsigned *v=faces[f];struct bench_point p=projected[v[0]],q=projected[v[1]],r=projected[v[2]];
            if((int64_t)(q.x-p.x)*(r.y-p.y)-(int64_t)(q.y-p.y)*(r.x-p.x)<=0)continue;
            struct bench_face face={.color=colors[(f+cube)%6]};for(unsigned i=0;i<4;++i){face.p[i]=projected[v[i]];face.depth+=depth[v[i]];}out[count++]=face;
        }
    }
    for(unsigned i=1;i<count;++i){struct bench_face face=out[i];unsigned j=i;while(j && out[j-1].depth<face.depth){out[j]=out[j-1];--j;}out[j]=face;}
    return count;
}
#endif
