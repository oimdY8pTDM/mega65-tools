/*
  Issue #390 - VIC-IV pixel RGB read-back
*/
#define ISSUE_NUM 390

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <random.h>
#include <tests.h>

unsigned char sub=0;

char msg[64 + 1];

unsigned short i, j;
unsigned char a, b, c, d;

unsigned short abs2(signed short s)
{
  if (s > 0)
    return s;
  return -s;
}

void graphics_clear_screen(void)
{
  lfill(0x40000L, 0, 32768L);
  lfill(0x48000L, 0, 32768L);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000L, 0, 32768L);
  lfill(0x58000L, 0, 32768L);
}

void h640_text_mode(void)
{
  // lower case
  POKE(0xD018, 0x16);

  // Normal text mode
  POKE(0xD054, 0x00);
  // H640, fast CPU, extended attributes
  POKE(0xD031, 0xE0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 80 chars per row
  POKE(0xD05E, 80);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  lfill(0xc000, 0x20, 2000);
  // Clear colour RAM
  lfill(0xff80000L, 0x0E, 2000);
}

void graphics_mode(void)
{
  // 16-bit text mode, full-colour text for high chars
  POKE(0xD054, 0x05);
  // H320, fast CPU
  POKE(0xD031, 0x40);
  // 320x200 per char, 16 pixels wide per char
  // = 320/8 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 40 chars per row
  POKE(0xD05E, 40);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  // Layout screen so that graphics data comes from $40000 -- $4FFFF

  i = 0x40000 / 0x40;
  for (a = 0; a < 40; a++)
    for (b = 0; b < 25; b++) {
      POKE(0xC000 + b * 80 + a * 2 + 0, i & 0xff);
      POKE(0xC000 + b * 80 + a * 2 + 1, i >> 8);

      i++;
    }

  // Clear colour RAM
  for (i = 0; i < 2000; i++) {
    lpoke(0xff80000L + 0 + i, 0x00);
    lpoke(0xff80000L + 1 + i, 0x00);
  }

  POKE(0xD020, 0);
  POKE(0xD021, 0);
}

void print_text(unsigned char x, unsigned char y, unsigned char colour, char* msg);

unsigned short pixel_addr;
unsigned char pixel_temp;
void plot_pixel(unsigned short x, unsigned char y, unsigned char colour)
{
  pixel_addr = (x & 0x7) + 64 * 25 * (x >> 3);
  pixel_addr += y << 3;

  lpoke(0x50000L + pixel_addr, colour);
}

void plot_pixel_direct(unsigned short x, unsigned char y, unsigned char colour)
{
  pixel_addr = (x & 0x7) + 64 * 25 * (x >> 3);
  pixel_addr += y << 3;

  lpoke(0x40000L + pixel_addr, colour);
}

unsigned char char_code;
void print_text(unsigned char x, unsigned char y, unsigned char colour, char* msg)
{
  pixel_addr = 0xC000 + x * 2 + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    POKE(pixel_addr + 0, char_code);
    POKE(pixel_addr + 1, 0);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 0, 0x00);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 1, colour);
    msg++;
    pixel_addr += 2;
  }
}

void print_text80(unsigned char x, unsigned char y, unsigned char colour, char* msg)
{
  pixel_addr = 0xC000 + x + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    else if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    else if (*msg >= 0x60 && *msg <= 0x7A)
      char_code = *msg - 0x20;
    POKE(pixel_addr + 0, char_code);
    lpoke(0xff80000L - 0xc000 + pixel_addr, colour);
    msg++;
    pixel_addr += 1;
  }
}

void activate_double_buffer(void)
{
  lcopy(0x50000, 0x40000, 0x8000);
  lcopy(0x58000, 0x48000, 0x8000);
}

unsigned char fd;
int count;
unsigned char buffer[512];

unsigned long load_addr;

unsigned char line_dmalist[256];

unsigned char ofs;
unsigned char slope_ofs, line_mode_ofs, cmd_ofs, count_ofs;
unsigned char src_ofs, dst_ofs;

int Axx, Axy, Axz;
int Ayx, Ayy, Ayz;
int Azx, Azy, Azz;

signed char sin_table[256] = { 0x80, 0x83, 0x86, 0x89, 0x8c, 0x8f, 0x92, 0x95, 0x98, 0x9b, 0x9e, 0xa1, 0xa4, 0xa7, 0xaa,
  0xad, 0xb0, 0xb3, 0xb6, 0xb9, 0xbb, 0xbe, 0xc1, 0xc3, 0xc6, 0xc9, 0xcb, 0xce, 0xd0, 0xd2, 0xd5, 0xd7, 0xd9, 0xdb, 0xde,
  0xe0, 0xe2, 0xe4, 0xe6, 0xe7, 0xe9, 0xeb, 0xec, 0xee, 0xf0, 0xf1, 0xf2, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb,
  0xfb, 0xfc, 0xfd, 0xfd, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfd, 0xfd, 0xfc, 0xfb, 0xfb,
  0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4, 0xf2, 0xf1, 0xf0, 0xee, 0xec, 0xeb, 0xe9, 0xe7, 0xe6, 0xe4, 0xe2, 0xe0, 0xde,
  0xdb, 0xd9, 0xd7, 0xd5, 0xd2, 0xd0, 0xce, 0xcb, 0xc9, 0xc6, 0xc3, 0xc1, 0xbe, 0xbb, 0xb9, 0xb6, 0xb3, 0xb0, 0xad, 0xaa,
  0xa7, 0xa4, 0xa1, 0x9e, 0x9b, 0x98, 0x95, 0x92, 0x8f, 0x8c, 0x89, 0x86, 0x83, 0x80, 0x7c, 0x79, 0x76, 0x73, 0x70, 0x6d,
  0x6a, 0x67, 0x64, 0x61, 0x5e, 0x5b, 0x58, 0x55, 0x52, 0x4f, 0x4c, 0x49, 0x46, 0x44, 0x41, 0x3e, 0x3c, 0x39, 0x36, 0x34,
  0x31, 0x2f, 0x2d, 0x2a, 0x28, 0x26, 0x24, 0x21, 0x1f, 0x1d, 0x1b, 0x19, 0x18, 0x16, 0x14, 0x13, 0x11, 0x0f, 0x0e, 0x0d,
  0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x04, 0x03, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x11, 0x13,
  0x14, 0x16, 0x18, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x26, 0x28, 0x2a, 0x2d, 0x2f, 0x31, 0x34, 0x36, 0x39, 0x3c, 0x3e,
  0x41, 0x44, 0x46, 0x49, 0x4c, 0x4f, 0x52, 0x55, 0x58, 0x5b, 0x5e, 0x61, 0x64, 0x67, 0x6a, 0x6d, 0x70, 0x73, 0x76, 0x79,
  0x7c };
signed char cos_table[256] = { 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfd, 0xfd, 0xfc, 0xfb, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7,
  0xf6, 0xf5, 0xf4, 0xf2, 0xf1, 0xf0, 0xee, 0xec, 0xeb, 0xe9, 0xe7, 0xe6, 0xe4, 0xe2, 0xe0, 0xde, 0xdb, 0xd9, 0xd7, 0xd5,
  0xd2, 0xd0, 0xce, 0xcb, 0xc9, 0xc6, 0xc3, 0xc1, 0xbe, 0xbb, 0xb9, 0xb6, 0xb3, 0xb0, 0xad, 0xaa, 0xa7, 0xa4, 0xa1, 0x9e,
  0x9b, 0x98, 0x95, 0x92, 0x8f, 0x8c, 0x89, 0x86, 0x83, 0x80, 0x7c, 0x79, 0x76, 0x73, 0x70, 0x6d, 0x6a, 0x67, 0x64, 0x61,
  0x5e, 0x5b, 0x58, 0x55, 0x52, 0x4f, 0x4c, 0x49, 0x46, 0x44, 0x41, 0x3e, 0x3c, 0x39, 0x36, 0x34, 0x31, 0x2f, 0x2d, 0x2a,
  0x28, 0x26, 0x24, 0x21, 0x1f, 0x1d, 0x1b, 0x19, 0x18, 0x16, 0x14, 0x13, 0x11, 0x0f, 0x0e, 0x0d, 0x0b, 0x0a, 0x09, 0x08,
  0x07, 0x06, 0x05, 0x04, 0x04, 0x03, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02,
  0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x11, 0x13, 0x14, 0x16, 0x18, 0x19,
  0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x26, 0x28, 0x2a, 0x2d, 0x2f, 0x31, 0x34, 0x36, 0x39, 0x3c, 0x3e, 0x41, 0x44, 0x46, 0x49,
  0x4c, 0x4f, 0x52, 0x55, 0x58, 0x5b, 0x5e, 0x61, 0x64, 0x67, 0x6a, 0x6d, 0x70, 0x73, 0x76, 0x79, 0x7c, 0x80, 0x83, 0x86,
  0x89, 0x8c, 0x8f, 0x92, 0x95, 0x98, 0x9b, 0x9e, 0xa1, 0xa4, 0xa7, 0xaa, 0xad, 0xb0, 0xb3, 0xb6, 0xb9, 0xbb, 0xbe, 0xc1,
  0xc3, 0xc6, 0xc9, 0xcb, 0xce, 0xd0, 0xd2, 0xd5, 0xd7, 0xd9, 0xdb, 0xde, 0xe0, 0xe2, 0xe4, 0xe6, 0xe7, 0xe9, 0xeb, 0xec,
  0xee, 0xf0, 0xf1, 0xf2, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfb, 0xfc, 0xfd, 0xfd, 0xfe, 0xfe, 0xfe, 0xfe,
  0xfe };

unsigned long mul32(unsigned short a, unsigned short b)
{
  POKE(0xD770, a);
  POKE(0xD771, a >> 8);
  POKE(0xD772, 0);
  POKE(0xD773, 0);
  POKE(0xD774, b);
  POKE(0xD775, b >> 8);
  POKE(0xD776, 0);
  POKE(0xD777, 0);
  return PEEK(0xD778) + (PEEK(0xD779) << 8) + (((long)PEEK(0xD77A)) << 16) + (((long)PEEK(0xD77B)) << 24);
}

#if 1
void rotate_setup(unsigned char pitch, unsigned char roll, unsigned char yaw)
{
  char cosa = cos_table[yaw];
  char sina = sin_table[yaw];

  char cosb = cos_table[pitch];
  char sinb = sin_table[pitch];

  char cosc = cos_table[roll];
  char sinc = sin_table[roll];

  Axx = mul32(cosa, cosb);

  Axy = mul32(mul32(cosa, sinb), sinc) - mul32(sina, cosc);
  Axz = mul32(mul32(cosa, sinb), cosc) + mul32(sina, sinc);

  Ayx = mul32(sina, cosb);
  Ayy = mul32(mul32(sina, sinb), sinc) + mul32(cosa, cosc);
  Ayz = mul32(mul32(sina, sinb), cosc) - mul32(cosa, sinc);

  Azx = -sinb;
  Azy = mul32(cosb, sinc);
  Azz = mul32(cosb, cosc);

#if 0
    for (var i = 0; i < points.length; i++) {
        var px = points[i].x;
        var py = points[i].y;
        var pz = points[i].z;

        points[i].x = Axx*px + Axy*py + Axz*pz;
        points[i].y = Ayx*px + Ayy*py + Ayz*pz;
        points[i].z = Azx*px + Azy*py + Azz*pz;
    }
#endif
}
#endif

void draw_line(int x1, int y1, int x2, int y2, unsigned char colour)
{
  long addr;
  int temp, slope, dx, dy;

  // Ignore if we choose to draw a point
  if (x2 == x1 && y2 == y1)
    return;

  dx = x2 - x1;
  dy = y2 - y1;
  if (dx < 0)
    dx = -dx;
  if (dy < 0)
    dy = -dy;

  //  snprintf(msg,41,"(%d,%d) - (%d,%d)    ",x1,y1,x2,y2);
  //  print_text(0,1,1,msg);

  // Draw line from x1,y1 to x2,y2
  if (dx < dy) {
    // Y is major axis

    if (y2 < y1) {
      temp = x1;
      x1 = x2;
      x2 = temp;
      temp = y1;
      y1 = y2;
      y2 = temp;
    }

    // Use hardware divider to get the slope
    POKE(0xD770, dx & 0xff);
    POKE(0xD771, dx >> 8);
    POKE(0xD772, 0);
    POKE(0xD773, 0);
    POKE(0xD774, dy & 0xff);
    POKE(0xD775, dy >> 8);
    POKE(0xD776, 0);
    POKE(0xD777, 0);

    // Wait 16 cycles
    POKE(0xD020, 0);
    POKE(0xD020, 0);
    POKE(0xD020, 0);
    POKE(0xD020, 0);

    // Slope is the most significant bytes of the fractional part
    // of the division result
    slope = PEEK(0xD76A) + (PEEK(0xD76B) << 8);

    // Put slope into DMA options
    line_dmalist[slope_ofs] = slope & 0xff;
    line_dmalist[slope_ofs + 2] = slope >> 8;

    // Load DMA dest address with the address of the first pixel
    addr = 0x40000 + (y1 << 3) + (x1 & 7) + (x1 >> 3) * 64 * 25L;
    line_dmalist[dst_ofs + 0] = addr & 0xff;
    line_dmalist[dst_ofs + 1] = addr >> 8;
    line_dmalist[dst_ofs + 2] = (addr >> 16) & 0xf;

    // Source is the colour
    line_dmalist[src_ofs] = colour & 0xf;

    // Count is number of pixels, i.e., dy.
    line_dmalist[count_ofs] = dy & 0xff;
    line_dmalist[count_ofs + 1] = dy >> 8;

    // Command is FILL
    line_dmalist[cmd_ofs] = 0x03;

    // Line mode active, major axis is Y
    line_dmalist[line_mode_ofs] = 0x80 + 0x40 + (((x2 - x1) < 0) ? 0x20 : 0x00);

    //    snprintf(msg,41,"Y: (%d,%d) - (%d,%d) m=%04x",x1,y1,x2,y2,slope);
    //    print_text(0,2,1,msg);

    POKE(0xD020, 1);

    POKE(0xD701, ((unsigned int)(&line_dmalist)) >> 8);
    POKE(0xD705, ((unsigned int)(&line_dmalist)) >> 0);

    POKE(0xD020, 0);
  }
  else {
    // X is major axis

    if (x2 < x1) {
      temp = x1;
      x1 = x2;
      x2 = temp;
      temp = y1;
      y1 = y2;
      y2 = temp;
    }

    // Use hardware divider to get the slope
    POKE(0xD770, dy & 0xff);
    POKE(0xD771, dy >> 8);
    POKE(0xD772, 0);
    POKE(0xD773, 0);
    POKE(0xD774, dx & 0xff);
    POKE(0xD775, dx >> 8);
    POKE(0xD776, 0);
    POKE(0xD777, 0);

    // Wait 16 cycles
    POKE(0xD020, 0);
    POKE(0xD020, 0);
    POKE(0xD020, 0);
    POKE(0xD020, 0);

    // Slope is the most significant bytes of the fractional part
    // of the division result
    slope = PEEK(0xD76A) + (PEEK(0xD76B) << 8);

    // Put slope into DMA options
    line_dmalist[slope_ofs] = slope & 0xff;
    line_dmalist[slope_ofs + 2] = slope >> 8;

    // Load DMA dest address with the address of the first pixel
    addr = 0x40000 + (y1 << 3) + (x1 & 7) + (x1 >> 3) * 64 * 25;
    line_dmalist[dst_ofs + 0] = addr & 0xff;
    line_dmalist[dst_ofs + 1] = addr >> 8;
    line_dmalist[dst_ofs + 2] = (addr >> 16) & 0xf;

    // Source is the colour
    line_dmalist[src_ofs] = colour & 0xf;

    // Count is number of pixels, i.e., dy.
    line_dmalist[count_ofs] = dx & 0xff;
    line_dmalist[count_ofs + 1] = dx >> 8;

    // Command is FILL
    line_dmalist[cmd_ofs] = 0x03;

    // Line mode active, major axis is X
    line_dmalist[line_mode_ofs] = 0x80 + 0x00 + (((y2 - y1) < 0) ? 0x20 : 0x00);

    //    snprintf(msg,41,"X: (%d,%d) - (%d,%d) m=%04x",x1,y1,x2,y2,slope);
    //    print_text(0,2,1,msg);

    POKE(0xD020, 1);

    POKE(0xD701, ((unsigned int)(&line_dmalist)) >> 8);
    POKE(0xD705, ((unsigned int)(&line_dmalist)) >> 0);

    POKE(0xD020, 0);
  }
}

unsigned char nyblswap(unsigned char c)
{
  return (c >> 4) | (c << 4);
}

unsigned char led[12];
unsigned char red,green,blue;
unsigned char frame_num;

void read_pixel(short x,short y,unsigned char *red,unsigned char *green,unsigned char *blue)
{
  // Select (128,128) as pixel to read back
  POKE(0xD07D,x);
  POKE(0xD07E,y);
  POKE(0xD07F,(x>>8)+((y>>8)<<4));

  // Wait at least one whole frame
  frame_num=PEEK(0xD7FA); while(PEEK(0xD7FA)==frame_num) continue;
  frame_num=PEEK(0xD7FA); while(PEEK(0xD7FA)==frame_num) continue;

  POKE(0xD07C,0x52); *red=PEEK(0xD07D);
  POKE(0xD07C,0x92); *green=PEEK(0xD07D);
  POKE(0xD07C,0xD2); *blue=PEEK(0xD07D);
}
  


void main(void)
{
  unsigned char playing = 0;

  asm("sei");

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  POKE(0xD020, 0);
  POKE(0xD021, 0);

  // Stop all DMA audio first
  POKE(0xD720, 0);
  POKE(0xD730, 0);
  POKE(0xD740, 0);
  POKE(0xD750, 0);

  graphics_mode();
  graphics_clear_screen();

  graphics_clear_double_buffer();
  activate_double_buffer();

  // Disable hot registers
  POKE(0xD05D, PEEK(0xD05D) & 0x7f);

  // Set background colour to light grey
  POKE(0xD021, 0x0b);

  print_text(0, 0, 1, "Issue #390 - VIC-IV Pixel read-back");

  // Check a background pixel
  read_pixel(0x80,0x80,&red,&green,&blue);
  snprintf(msg,64,"Background Pixel @ (128,128)=#%02x%02x%02x",red,green,blue);
  print_text(0,2,14,msg);

  if (red==0x50&&green==0x50&&blue==0x50) unit_test_report(ISSUE_NUM,sub++,TEST_PASS);
  else  unit_test_report(ISSUE_NUM,sub++,TEST_FAIL);
  
  // Now move to a character pixel
  read_pixel(0x80,0x74,&red,&green,&blue);
  snprintf(msg,64,"White Char Pixel @ (0x80,0x74)=#%02x%02x%02x",red,green,blue);
  print_text(0,3,14,msg);

  if (red==0xF0&&green==0xF0&&blue==0xF0) unit_test_report(ISSUE_NUM,sub++,TEST_PASS);
  else  unit_test_report(ISSUE_NUM,sub++,TEST_FAIL);

  // And now one that has different values in each of the RGB channels
  read_pixel(0x80,0x8c,&red,&green,&blue);
  snprintf(msg,64,"Light Blue Pixel @ (0x80,0x8C)=#%02x%02x%02x",red,green,blue);
  print_text(0,4,14,msg);

  if (red==0x90&&green==0x90&&blue==0xF0) unit_test_report(ISSUE_NUM,sub++,TEST_PASS);
  else  unit_test_report(ISSUE_NUM,sub++,TEST_FAIL);
  
  // Now change the colour of that pixel and read it again
  POKE(0xd8a5,8);
  read_pixel(0x80,0x8c,&red,&green,&blue);
  snprintf(msg,64,"Orange Char Pixel @ (0x80,0x8C)=#%02x%02x%02x",red,green,blue);
  print_text(0,5,14,msg);

  if (red==0xf0&&green==0x60&&blue==0x00) unit_test_report(ISSUE_NUM,sub++,TEST_PASS);
  else  unit_test_report(ISSUE_NUM,sub++,TEST_FAIL);

  // Now put a sprite there
  POKE(0xD015,0x01);
  POKE(0xD000,0x20);
  POKE(0xD001,0x40);
  POKE(0xD027,0x0d); // light green
  POKE(0x7F8,0x3c0/0x40); // Sprite in the cassette buffer
  lfill(0x3c0,0xff,0x40); // solid sprite

  read_pixel(0x80,0x8c,&red,&green,&blue);
  snprintf(msg,64,"Sprite Pixel @ (0x80,0x8C)=#%02x%02x%02x",red,green,blue);
  print_text(0,6,14,msg);

  if (red==0x90&&green==0xf0&&blue==0x90) unit_test_report(ISSUE_NUM,sub++,TEST_PASS);
  else  unit_test_report(ISSUE_NUM,sub++,TEST_FAIL);

  // Hide sprite again
  POKE(0xD015,0);
  
  // Report completion of tests
  unit_test_report(ISSUE_NUM,sub++,TEST_DONEALL);
  
}
