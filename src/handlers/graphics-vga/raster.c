#include "raster.h"
static int identity(const struct gfx_surface *s){return s->matrix[0]==65536 && !s->matrix[1] && !s->matrix[2] && s->matrix[3]==65536 && !s->matrix[4] && !s->matrix[5];}
void gfx_damage(struct gfx_surface *s,struct gfx_rect r){
    if(r.width<=0 || r.height<=0)return;
    if(!s->dirty){s->damage=r;s->dirty=1;return;}
    int32_t right=s->damage.x+s->damage.width,bottom=s->damage.y+s->damage.height;
    if(r.x+r.width>right)right=r.x+r.width;
    if(r.y+r.height>bottom)bottom=r.y+r.height;
    if(r.x<s->damage.x)s->damage.x=r.x;
    if(r.y<s->damage.y)s->damage.y=r.y;
    s->damage.width=right-s->damage.x;s->damage.height=bottom-s->damage.y;
}
static void changed_row(struct gfx_surface *s,int32_t y,int32_t first,int32_t last,uint64_t count){
    if(!count)return;
    if(first>last){int32_t tmp=first;first=last;last=tmp;}
    gfx_damage(s,(struct gfx_rect){first,y,last-first+1,1});
    s->generation=count>UINT64_MAX-s->generation?UINT64_MAX:s->generation+count;
}
int gfx_copy_region(const struct gfx_surface *s,struct gfx_point at,struct gfx_rect from,unsigned opacity,struct gfx_point *destination,struct gfx_rect *source){
    if(opacity!=255 || s->mode!=GFX_SOURCE || !identity(s) || from.x<0 || from.y<0 || from.width<0 || from.height<0 || (uint32_t)from.x>s->width || (uint32_t)from.y>s->height || (uint32_t)from.width>s->width-from.x || (uint32_t)from.height>s->height-from.y || at.x<-32768 || at.x>32767 || at.y<-32768 || at.y>32767)return 0;
    int32_t left=at.x>s->clip.x?at.x:s->clip.x,top=at.y>s->clip.y?at.y:s->clip.y;
    int32_t right=at.x+from.width,bottom=at.y+from.height;
    if(right>s->clip.x+s->clip.width)right=s->clip.x+s->clip.width;
    if(bottom>s->clip.y+s->clip.height)bottom=s->clip.y+s->clip.height;
    if(right<left)right=left;
    if(bottom<top)bottom=top;
    *destination=(struct gfx_point){left,top};*source=(struct gfx_rect){from.x+left-at.x,from.y+top-at.y,right-left,bottom-top};return 1;
}
int gfx_self_blit(struct gfx_surface *s,struct gfx_point at,struct gfx_rect from,unsigned opacity){
    struct gfx_point dest;struct gfx_rect src;if(!gfx_copy_region(s,at,from,opacity,&dest,&src))return 0;
    if(!src.width || !src.height)return 1;
    int backwards_y=dest.y>src.y,backwards_x=dest.y==src.y && dest.x>src.x;
    for(int32_t row=0;row<src.height;++row){int32_t y=backwards_y?src.height-1-row:row;
        uint32_t *out=s->pixels+(size_t)(dest.y+y)*s->width+dest.x;const uint32_t *in=s->pixels+(size_t)(src.y+y)*s->width+src.x;
        int32_t first=0,last=0;uint64_t count=0;
        for(int32_t column=0;column<src.width;++column){int32_t x=backwards_x?src.width-1-column:column;uint32_t pixel=in[x];if(out[x]!=pixel){out[x]=pixel;if(!count)first=x;last=x;++count;}}
        changed_row(s,dest.y+y,dest.x+first,dest.x+last,count);
    }
    return 1;
}
int gfx_fill_rect(struct gfx_surface *s,struct gfx_rect r,uint32_t color){
    if(!identity(s) || (s->mode!=GFX_SOURCE && (s->mode!=GFX_SOURCE_OVER || (color>>24)!=255)))return 0;
    int32_t left=r.x>s->clip.x?r.x:s->clip.x,top=r.y>s->clip.y?r.y:s->clip.y;
    int64_t right=(int64_t)r.x+r.width,bottom=(int64_t)r.y+r.height;
    if(right>s->clip.x+s->clip.width)right=s->clip.x+s->clip.width;
    if(bottom>s->clip.y+s->clip.height)bottom=s->clip.y+s->clip.height;
    if(r.width<=0 || r.height<=0 || right<=left || bottom<=top)return 1;
    for(int32_t y=top;y<bottom;++y){uint32_t *out=s->pixels+(size_t)y*s->width;int32_t first=0,last=0;uint64_t count=0;
        for(int32_t x=left;x<right;++x)if(out[x]!=color){out[x]=color;if(!count)first=x;last=x;++count;}
        changed_row(s,y,first,last,count);
    }return 1;
}
#include <limits.h>
static int32_t max(int32_t a,int32_t b){return a>b?a:b;}
static int32_t min(int32_t a,int32_t b){return a<b?a:b;}
static int32_t clamp(int64_t x){return x< -32768?-32768:x>32767?32767:(int32_t)x;}
void gfx_init(struct gfx_surface *s,uint32_t *p,uint32_t w,uint32_t h){*s=(struct gfx_surface){.pixels=p,.width=w,.height=h,.clip={0,0,(int32_t)w,(int32_t)h},.matrix={65536,0,0,65536,0,0}};}
uint32_t gfx_rgb(uint32_t argb){unsigned a=argb>>24;if(a==255)return argb&0xffffff;uint32_t rgb=0;for(unsigned shift=0;shift<24;shift+=8)rgb|=((((argb>>shift)&255)*a+127)/255)<<shift;return rgb;}
int gfx_uniform_rgb(const struct gfx_surface *s,struct gfx_rect r,uint32_t *out){
    if(!out || r.x<0 || r.y<0 || r.width<=0 || r.height<=0 || (uint32_t)r.x>s->width || (uint32_t)r.y>s->height || (uint32_t)r.width>s->width-r.x || (uint32_t)r.height>s->height-r.y)return 0;
    uint32_t first=s->pixels[(size_t)r.y*s->width+r.x],rgb=gfx_rgb(first);
    for(int32_t y=0;y<r.height;++y)for(int32_t x=0;x<r.width;++x){uint32_t pixel=s->pixels[(size_t)(r.y+y)*s->width+r.x+x];if(pixel!=first && gfx_rgb(pixel)!=rgb)return 0;}
    *out=rgb;return 1;
}
void gfx_reset_damage(struct gfx_surface *s){s->dirty=0;s->damage=(struct gfx_rect){0};}
int gfx_transform(struct gfx_surface *s,const int32_t m[6]){for(unsigned i=0;i<4;++i)if(m[i]<-1048576 || m[i]>1048576)return 0;for(unsigned i=0;i<6;++i)s->matrix[i]=m[i];return 1;}
void gfx_clip(struct gfx_surface *s,struct gfx_rect r){int64_t right=(int64_t)r.x+r.width,bottom=(int64_t)r.y+r.height;int32_t x=max(r.x,0),y=max(r.y,0);int32_t endx=right<0?0:right>s->width?(int32_t)s->width:(int32_t)right,endy=bottom<0?0:bottom>s->height?(int32_t)s->height:(int32_t)bottom;s->clip=(struct gfx_rect){x,y,max(0,endx-x),max(0,endy-y)};}
struct gfx_point gfx_map(const struct gfx_surface *s,struct gfx_point p){return (struct gfx_point){clamp(((int64_t)s->matrix[0]*p.x+(int64_t)s->matrix[2]*p.y+s->matrix[4])/65536),clamp(((int64_t)s->matrix[1]*p.x+(int64_t)s->matrix[3]*p.y+s->matrix[5])/65536)};}
static uint32_t blend(uint32_t src,uint32_t dst){unsigned a=src>>24,b=dst>>24,out=a+(b*(255-a)+127)/255;if(!out)return 0;uint32_t result=out<<24;for(unsigned shift=0;shift<24;shift+=8){uint64_t numerator=(uint64_t)((src>>shift)&255)*a*255+(uint64_t)((dst>>shift)&255)*b*(255-a);result|=(uint32_t)((numerator+out*127)/(out*255))<<shift;}return result;}
void gfx_pixel(struct gfx_surface *s,int32_t x,int32_t y,uint32_t color){if(x<s->clip.x || y<s->clip.y || x-s->clip.x>=s->clip.width || y-s->clip.y>=s->clip.height)return;uint32_t *p=&s->pixels[(size_t)y*s->width+x];if(s->mode==GFX_SOURCE_OVER)color=blend(color,*p);if(*p==color)return;*p=color;if(!s->dirty){s->damage=(struct gfx_rect){x,y,1,1};s->dirty=1;}else{int32_t right=max(s->damage.x+s->damage.width,x+1),bottom=max(s->damage.y+s->damage.height,y+1);s->damage.x=min(s->damage.x,x);s->damage.y=min(s->damage.y,y);s->damage.width=right-s->damage.x;s->damage.height=bottom-s->damage.y;}if(s->generation!=UINT64_MAX)++s->generation;}
static unsigned outcode(const struct gfx_surface *s,struct gfx_point p){return (p.x<s->clip.x?1:0)|(p.x>=s->clip.x+s->clip.width?2:0)|(p.y<s->clip.y?4:0)|(p.y>=s->clip.y+s->clip.height?8:0);}
static void device_line(struct gfx_surface *s,struct gfx_point a,struct gfx_point b,uint32_t color){if(!s->clip.width || !s->clip.height)return;for(unsigned tries=0;tries<8;++tries){unsigned ca=outcode(s,a),cb=outcode(s,b);if(!(ca|cb))break;if(ca&cb)return;unsigned c=ca?ca:cb;int64_t x=a.x,y=a.y,dx=(int64_t)b.x-a.x,dy=(int64_t)b.y-a.y;if(c&12){if(!dy)return;y=c&4?s->clip.y:s->clip.y+s->clip.height-1;x=a.x+dx*(y-a.y)/dy;}else{if(!dx)return;x=c&1?s->clip.x:s->clip.x+s->clip.width-1;y=a.y+dy*(x-a.x)/dx;}if(ca)a=(struct gfx_point){x,y};else b=(struct gfx_point){x,y};}if(outcode(s,a)|outcode(s,b))return;int32_t dx=a.x>b.x?a.x-b.x:b.x-a.x,sx=a.x<b.x?1:-1,dy=-(a.y>b.y?a.y-b.y:b.y-a.y),sy=a.y<b.y?1:-1,error=dx+dy;for(;;){gfx_pixel(s,a.x,a.y,color);if(a.x==b.x && a.y==b.y)break;int32_t twice=2*error;if(twice>=dy){error+=dy;a.x+=sx;}if(twice<=dx){error+=dx;a.y+=sy;}}}
void gfx_line(struct gfx_surface *s,struct gfx_point a,struct gfx_point b,uint32_t color){device_line(s,gfx_map(s,a),gfx_map(s,b),color);}
/* INT32_MIN separates contours; even-odd filling applies across all contours. */
void gfx_polygon(struct gfx_surface *s,const struct gfx_point *p,size_t count,uint32_t color,int fill){
    if(count<2)return;
    if(!fill){size_t begin=0;while(begin<count){size_t end=begin;while(end<count && p[end].x!=INT32_MIN)++end;for(size_t i=begin+1;i<end;++i)gfx_line(s,p[i-1],p[i],color);if(end>begin+1)gfx_line(s,p[end-1],p[begin],color);begin=end+1;}return;}
    int32_t left=32767,right=-32768,top=32767,bottom=-32768;for(size_t i=0;i<count;++i){if(p[i].x==INT32_MIN)continue;struct gfx_point q=gfx_map(s,p[i]);left=min(left,q.x);right=max(right,q.x);top=min(top,q.y);bottom=max(bottom,q.y);}left=max(left,s->clip.x);right=min(right,s->clip.x+s->clip.width);top=max(top,s->clip.y);bottom=min(bottom,s->clip.y+s->clip.height);
    for(int32_t y=top;y<bottom;++y)for(int32_t x=left;x<right;++x){unsigned inside=0;size_t begin=0;while(begin<count){size_t end=begin;while(end<count && p[end].x!=INT32_MIN)++end;if(end>begin){struct gfx_point a=gfx_map(s,p[end-1]);for(size_t i=begin;i<end;++i){struct gfx_point b=gfx_map(s,p[i]);if((a.y>y)!=(b.y>y)){int64_t crossing=a.x+(int64_t)(b.x-a.x)*(y-a.y)/(b.y-a.y);if(x<crossing)inside^=1;}a=b;}}begin=end+1;}if(inside)gfx_pixel(s,x,y,color);}
}
void gfx_rect(struct gfx_surface *s,struct gfx_rect r,uint32_t color,int fill){if(fill && gfx_fill_rect(s,r,color))return;if(r.width<=0 || r.height<=0)return;struct gfx_point p[4]={{r.x,r.y},{clamp((int64_t)r.x+r.width-(fill?0:1)),r.y},{clamp((int64_t)r.x+r.width-(fill?0:1)),clamp((int64_t)r.y+r.height-(fill?0:1))},{r.x,clamp((int64_t)r.y+r.height-(fill?0:1))}};gfx_polygon(s,p,4,color,fill);}
/* CORDIC, angles in degrees 16.16, sine/cosine in 16.16. */
static void gfx_sincos(int32_t angle,int32_t *cosine,int32_t *sine){static const int32_t atan_turn[]={536870912,316933406,167458907,85004756,42667331,21354465,10679838,5340245,2670163,1335087,667544,333772,166886,83443,41722,20861};int64_t z=(int64_t)angle*65536/360;while(z>INT32_MAX)z-=UINT64_C(1)<<32;while(z<INT32_MIN)z+=UINT64_C(1)<<32;int32_t sign=1;if(z>1073741824){z-=2147483648LL;sign=-1;}else if(z< -1073741824){z+=2147483648LL;sign=-1;}int32_t x=39797,y=0;for(unsigned i=0;i<16;++i){int32_t old=x;if(z>=0){x-=y>>i;y+=old>>i;z-=atan_turn[i];}else{x+=y>>i;y-=old>>i;z+=atan_turn[i];}}*cosine=x*sign;*sine=y*sign;}
static int64_t floor_div(int64_t n,int64_t d){int64_t q=n/d;if(n%d && ((n<0)!=(d<0)))--q;return q;}
static void filled_arc(struct gfx_surface *s,struct gfx_rect r,int32_t start,int32_t sweep,uint32_t color){
    if(r.width>32767 || r.height>32767)return;
    int64_t determinant=(int64_t)s->matrix[0]*s->matrix[3]-(int64_t)s->matrix[1]*s->matrix[2];if(!determinant)return;
    struct gfx_point corners[4]={{r.x,r.y},{clamp((int64_t)r.x+r.width),r.y},{r.x,clamp((int64_t)r.y+r.height)},{clamp((int64_t)r.x+r.width),clamp((int64_t)r.y+r.height)}};int32_t left=32767,right=-32768,top=32767,bottom=-32768;
    for(unsigned i=0;i<4;++i){struct gfx_point q=gfx_map(s,corners[i]);left=min(left,q.x);right=max(right,q.x);top=min(top,q.y);bottom=max(bottom,q.y);}left=max(left,s->clip.x);right=min(right,s->clip.x+s->clip.width);top=max(top,s->clip.y);bottom=min(bottom,s->clip.y+s->clip.height);
    int32_t c1,t1,c2,t2;int64_t first=start,last=(int64_t)start+sweep;if(sweep<0){first=last;last=start;}gfx_sincos(first%(360*65536),&c1,&t1);gfx_sincos(last%(360*65536),&c2,&t2);int64_t angle=sweep<0?-(int64_t)sweep:sweep,w2=(int64_t)r.width*r.width,h2=(int64_t)r.height*r.height;
    for(int32_t y=top;y<bottom;++y)for(int32_t x=left;x<right;++x){int64_t dx=(int64_t)x*65536+32768-s->matrix[4],dy=(int64_t)y*65536+32768-s->matrix[5];int64_t u=floor_div(dx*s->matrix[3]-dy*s->matrix[2],determinant)-r.x,v=floor_div(dy*s->matrix[0]-dx*s->matrix[1],determinant)-r.y;if(u<0 || v<0 || u>=r.width || v>=r.height)continue;u=u*2-r.width;v=v*2-r.height;if(u*u*h2+v*v*w2>w2*h2)continue;int64_t vx=u*r.height,vy=v*r.width,a=(int64_t)c1*vy-(int64_t)t1*vx,b=vx*t2-vy*c2;if(angle>=360*65536 || (angle>180*65536?(a>=0 || b>=0):(a>=0 && b>=0)))gfx_pixel(s,x,y,color);}
}
void gfx_arc(struct gfx_surface *s,struct gfx_rect r,int32_t start,int32_t sweep,uint32_t color,int fill){if(r.width<=0 || r.height<=0 || !sweep)return;if(sweep>360*65536)sweep=360*65536;if(sweep< -360*65536)sweep=-360*65536;if(fill){filled_arc(s,r,start,sweep,color);return;}unsigned steps=(unsigned)(sweep<0?-(int64_t)sweep:sweep)/32768+1;struct gfx_point center={clamp((int64_t)r.x+r.width/2),clamp((int64_t)r.y+r.height/2)},previous={0};for(unsigned i=0;i<=steps;++i){int32_t c,t;gfx_sincos((int32_t)(((int64_t)start+(int64_t)sweep*i/steps)%(360*65536)),&c,&t);struct gfx_point point={clamp((int64_t)center.x+(int64_t)c*r.width/131072),clamp((int64_t)center.y+(int64_t)t*r.height/131072)};if(i){if(fill){struct gfx_point triangle[3]={center,previous,point};gfx_polygon(s,triangle,3,color,1);}else gfx_line(s,previous,point,color);}previous=point;}}
void gfx_bezier(struct gfx_surface *s,const struct gfx_point p[4],uint32_t color){struct gfx_point previous=p[0];for(int64_t t=1;t<=128;++t){int64_t u=128-t;struct gfx_point point={clamp((u*u*u*p[0].x+3*u*u*t*p[1].x+3*u*t*t*p[2].x+t*t*t*p[3].x)/2097152),clamp((u*u*u*p[0].y+3*u*u*t*p[1].y+3*u*t*t*p[2].y+t*t*t*p[3].y)/2097152)};gfx_line(s,previous,point,color);previous=point;}}
void gfx_bitmap(struct gfx_surface *s,struct gfx_point origin,const uint32_t *pixels,uint32_t w,uint32_t h,uint32_t stride,unsigned opacity){
    int64_t determinant=(int64_t)s->matrix[0]*s->matrix[3]-(int64_t)s->matrix[1]*s->matrix[2];if(!determinant || !w || !h)return;
    struct gfx_point corners[4]={{origin.x,origin.y},{clamp((int64_t)origin.x+w),origin.y},{origin.x,clamp((int64_t)origin.y+h)},{clamp((int64_t)origin.x+w),clamp((int64_t)origin.y+h)}};int32_t left=32767,right=-32768,top=32767,bottom=-32768;
    for(unsigned i=0;i<4;++i){struct gfx_point p=gfx_map(s,corners[i]);left=min(left,p.x);right=max(right,p.x);top=min(top,p.y);bottom=max(bottom,p.y);}left=max(left,s->clip.x);right=min(right,s->clip.x+s->clip.width);top=max(top,s->clip.y);bottom=min(bottom,s->clip.y+s->clip.height);
    for(int32_t y=top;y<bottom;++y)for(int32_t x=left;x<right;++x){int64_t dx=(int64_t)x*65536-s->matrix[4],dy=(int64_t)y*65536-s->matrix[5];int64_t sx=floor_div(dx*s->matrix[3]-dy*s->matrix[2],determinant)-origin.x,sy=floor_div(dy*s->matrix[0]-dx*s->matrix[1],determinant)-origin.y;if(sx<0 || sy<0 || sx>=w || sy>=h)continue;uint32_t color=pixels[(size_t)sy*stride+sx];color=(color&0xffffff)|(((color>>24)*opacity+127)/255<<24);gfx_pixel(s,x,y,color);}
}
