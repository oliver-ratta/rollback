#include <math.h>
#include <stdio.h>
#include <stdalign.h>
const int fov = 90;
const int screen_width = 1280;
const int screen_height = 720;

void render_3d(int x, int y, int z, int *xp, int *yp, int fov){
   
    if (z == 0) z = 1;
    double half_fov_rad = (fov /2.0)* (3.1415926535897 / 180);
    double tan_val = tan(half_fov_rad);

    double projectx= (int)x/(z*tan_val);


    double projecty = (int)y/(z*tan_val);

    *xp = (int)(projectx * (screen_width / 2.0)) + (screen_width / 2);
    *yp = (int)(projecty * (screen_height / 2.0)) + (screen_height / 2);

    
}

int main(){

    int xsss = 0;
    int ysss = 0;

    render_3d(3,5,2, &xsss, &ysss, fov);
    printf("x: %d\n", xsss);
    printf("y: %d\n", ysss);


    return 0;

}


