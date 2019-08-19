#include <stdint.h>
#include <iostream>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "headers/MLX90640_API.h"
#include "lib/fb.h"

//#include <mcheck.h>

#define MLX_I2C_ADDR 0x33

#define IMAGE_SCALE 1

#include <stdint.h>
#include <iostream>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "headers/MLX90640_API.h"
#include "lib/fb.h"

//#include <mcheck.h>

#define MLX_I2C_ADDR 0x33

#define IMAGE_SCALE 1

// Valid frame rates are 1, 2, 4, 8, 16, 32 and 64
// The i2c baudrate is set to 1mhz to support these
#define FPS 2
#define FRAME_TIME_MICROS (1000000/FPS)

// Despite the framerate being ostensibly FPS hz
// The frame is often not ready in time
// This offset is added to the FRAME_TIME_MICROS
// to account for this.
//#define OFFSET_MICROS 850
#define OFFSET_MICROS 8500

int frame_num = 0;

FILE *binfile;
#define FILESIZERGB (32*24*3)
#define FILESIZEFLT (32*24*sizeof(float))
#define FILEPATH "/run/mlx90640-0.rgb"
#define FILELOCK "/run/mlx90640-0.lock"
#define FILEPATHFLT "/run/mlx90640-0.flt"

int fd;
char *map;
float *fltmap;

void *prepare_mmap(const char filepath[], long filesize) {
    int i;
    int result;
    void *map;

    fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0666);
    if (fd == -1) {
	perror("Error opening file");
	exit(EXIT_FAILURE);
    }

    result = lseek(fd, filesize-1, SEEK_SET);
    if (result == -1) {
	close(fd);
	perror("Error calling lseek()");
	exit(EXIT_FAILURE);
    }

    result = write(fd, "", 1);
    if (result != 1) {
	close(fd);
	perror("Error preparing file");
	exit(EXIT_FAILURE);
    }

    map = mmap(0, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
	close(fd);
	perror("Error mmapping the file");
	exit(EXIT_FAILURE);
    }

    return map;
}


void close_mmap(char *map, long filesize){
    /* Don't forget to free the mmapped memory
     */
    if (munmap(map, filesize) == -1) {
	perror("Error un-mmapping the file");
    }

    close(fd);
    return;
}

void put_pixel_false_colour(int x, int y, double v) {
    // Heatmap code borrowed from: http://www.andrewnoske.com/wiki/Code_-_heatmaps_and_color_gradients
    const int NUM_COLORS = 7;
    static float color[NUM_COLORS][3] = { {0,0,0}, {0,0,1}, {0,1,0}, {1,1,0}, {1,0,0}, {1,0,1}, {1,1,1} };
    int idx1, idx2;
    float fractBetween = 0;
    float vmin = 5.0;
    float vmax = 50.0;
    float vrange = vmax-vmin;
    float rawpix = v;
    v -= vmin;
    v /= vrange;
    if(v <= 0) {idx1=idx2=0;}
    else if(v >= 1) {idx1=idx2=NUM_COLORS-1;}
    else
    {
        v *= (NUM_COLORS-1);
        idx1 = floor(v);
        idx2 = idx1+1;
        fractBetween = v - float(idx1);
    }

    int ir, ig, ib;


    ir = (int)((((color[idx2][0] - color[idx1][0]) * fractBetween) + color[idx1][0]) * 255.0);
    ig = (int)((((color[idx2][1] - color[idx1][1]) * fractBetween) + color[idx1][1]) * 255.0);
    ib = (int)((((color[idx2][2] - color[idx1][2]) * fractBetween) + color[idx1][2]) * 255.0);

    //for(int px = 0; px < IMAGE_SCALE; px++){
     //   for(int py = 0; py < IMAGE_SCALE; py++){
            //fb_put_pixel(x + px, y + py, ir, ig, ib);
       //     fputc(ir, binfile);
       //     fputc(ig, binfile);
       //     fputc(ib, binfile);
	    //printf(" %03d%03d%03d", ir, ig, ib);
       // }
    //}
    map[(y + x * 32) * 3] = ir;
    map[(y + x * 32) * 3 + 1] = ig;
    map[(y + x * 32) * 3 + 2] = ib;

    fltmap[y + x * 32] = rawpix;
}

int main(){
    static uint16_t eeMLX90640[832];
    float emissivity = 1;
    uint16_t frame[834];
    static float image[768];
    static float mlx90640To[768];
    float eTa;
    static uint16_t data[768*sizeof(float)];

    // Must ensure only one copy of this program runs at a time.

     	                 /* l_type   l_whence  l_start  l_len  l_pid   */
    struct flock fl = { F_WRLCK, SEEK_SET, 0,       0,     0 };
    int fd;
    
    fl.l_pid = getpid();
    
    if ((fd = open(FILELOCK, O_RDWR)) == -1) {
    	perror("open");
        if ((fd = open(FILELOCK, O_RDWR|O_CREAT)) == -1) {
            exit(1);
        }
    }
    
    // Use F_SETLKW is waiting required.
    if (fcntl(fd, F_SETLK, &fl) == -1) {
    	perror("fcntl");
    	exit(1);
    }

    //printf("locked\n");
    
    //mtrace();

    map = (char *)prepare_mmap(FILEPATH, FILESIZERGB);
    fltmap = (float *)prepare_mmap(FILEPATHFLT, FILESIZEFLT);

    auto frame_time = std::chrono::microseconds(FRAME_TIME_MICROS + OFFSET_MICROS);

    MLX90640_SetDeviceMode(MLX_I2C_ADDR, 0);
    MLX90640_SetSubPageRepeat(MLX_I2C_ADDR, 0);
    switch(FPS){
        case 1:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b001);
            break;
        case 2:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b010);
            break;
        case 4:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b011);
            break;
        case 8:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b100);
            break;
        case 16:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b101);
            break;
        case 32:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b110);
            break;
        case 64:
            MLX90640_SetRefreshRate(MLX_I2C_ADDR, 0b111);
            break;
        default:
            printf("Unsupported framerate: %d", FPS);
            return 1;
    }
    MLX90640_SetChessMode(MLX_I2C_ADDR);

    paramsMLX90640 mlx90640;
    MLX90640_DumpEE(MLX_I2C_ADDR, eeMLX90640);
    MLX90640_ExtractParameters(eeMLX90640, &mlx90640);

    while (1){
    //for(int test_i = 0; test_i < 10; test_i++){

        auto start = std::chrono::system_clock::now();
        //auto start = std::chrono::high_resolution_clock::now();

        //printf("A\n");
        MLX90640_GetFrameData(MLX_I2C_ADDR, frame);

        //printf("B\n");
        MLX90640_InterpolateOutliers(frame, eeMLX90640);

        eTa = MLX90640_GetTa(frame, &mlx90640);
        MLX90640_CalculateTo(frame, &mlx90640, emissivity, eTa, mlx90640To);

        for(int y = 0; y < 24; y++){
            for(int x = 0; x < 32; x++){
                float val = mlx90640To[32 * (23-y) + x];
                put_pixel_false_colour((y*IMAGE_SCALE), (x*IMAGE_SCALE), val);
            }
        }

        //auto end = std::chrono::system_clock::now();
        //auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        //std::this_thread::sleep_for(std::chrono::microseconds(frame_time - elapsed));
        std::this_thread::sleep_for(std::chrono::microseconds(frame_time));
        //auto end = std::chrono::system_clock::now();
        //auto end = std::chrono::high_resolution_clock::now();
    }
    return 0;
}
