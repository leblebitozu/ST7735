/***************************************************
  This is a library for the Adafruit 1.8" SPI display.
  This library works with the Adafruit 1.8" TFT Breakout w/SD card
  ----> http://www.adafruit.com/products/358
  as well as Adafruit raw 1.8" TFT display
  ----> http://www.adafruit.com/products/618
 
  Check out the links above for our tutorials and wiring diagrams
  These displays use SPI to communicate, 4 or 5 pins are required to
  interface (RST is optional)
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/

#include <math.h>
#include <string.h>
#include "main.h"
#include "Adafruit_ST7735.h"
#include "hw_config.h"
#include "platform_config.h"

static uint16_t _width = ST7735_TFTWIDTH;
static uint16_t _height = ST7735_TFTHEIGHT;

// Rather than a bazillion lcd7735_sendCmd() and lcd7735_sendData() calls, screen
// initialization commands and arguments are organized in these tables
// stored in PROGMEM.  The table may look bulky, but that's mostly the
// formatting -- storage-wise this is hundreds of bytes more compact
// than the equivalent code.  Companion function follows.
#define DELAY 0x80

static const uint8_t Bcmd[] = {                  // Initialization commands for 7735B screens
    18,                       // 18 commands in list:
    ST7735_SWRESET,   DELAY,  //  1: Software reset, no args, w/delay
      50,                     //     50 ms delay
    ST7735_SLPOUT ,   DELAY,  //  2: Out of sleep mode, no args, w/delay
      255,                    //     255 = 500 ms delay
    ST7735_COLMOD , 1+DELAY,  //  3: Set color mode, 1 arg + delay:
      0x05,                   //     16-bit color 5-6-5 color format
      10,                     //     10 ms delay
    ST7735_FRMCTR1, 3+DELAY,  //  4: Frame rate control, 3 args + delay:
      0x00,                   //     fastest refresh
      0x06,                   //     6 lines front porch
      0x03,                   //     3 lines back porch
      10,                     //     10 ms delay
    ST7735_MADCTL , 1      ,  //  5: Memory access ctrl (directions), 1 arg:
      0x08,                   //     Row addr/col addr, bottom to top refresh
    ST7735_DISSET5, 2      ,  //  6: Display settings #5, 2 args, no delay:
      0x15,                   //     1 clk cycle nonoverlap, 2 cycle gate
                              //     rise, 3 cycle osc equalize
      0x02,                   //     Fix on VTL
    ST7735_INVCTR , 1      ,  //  7: Display inversion control, 1 arg:
      0x0,                    //     Line inversion
    ST7735_PWCTR1 , 2+DELAY,  //  8: Power control, 2 args + delay:
      0x02,                   //     GVDD = 4.7V
      0x70,                   //     1.0uA
      10,                     //     10 ms delay
    ST7735_PWCTR2 , 1      ,  //  9: Power control, 1 arg, no delay:
      0x05,                   //     VGH = 14.7V, VGL = -7.35V
    ST7735_PWCTR3 , 2      ,  // 10: Power control, 2 args, no delay:
      0x01,                   //     Opamp current small
      0x02,                   //     Boost frequency
    ST7735_VMCTR1 , 2+DELAY,  // 11: Power control, 2 args + delay:
      0x3C,                   //     VCOMH = 4V
      0x38,                   //     VCOML = -1.1V
      10,                     //     10 ms delay
    ST7735_PWCTR6 , 2      ,  // 12: Power control, 2 args, no delay:
      0x11, 0x15,
    ST7735_GMCTRP1,16      ,  // 13: Magical unicorn dust, 16 args, no delay:
      0x09, 0x16, 0x09, 0x20, //     (seriously though, not sure what
      0x21, 0x1B, 0x13, 0x19, //      these config values represent)
      0x17, 0x15, 0x1E, 0x2B,
      0x04, 0x05, 0x02, 0x0E,
    ST7735_GMCTRN1,16+DELAY,  // 14: Sparkles and rainbows, 16 args + delay:
      0x0B, 0x14, 0x08, 0x1E, //     (ditto)
      0x22, 0x1D, 0x18, 0x1E,
      0x1B, 0x1A, 0x24, 0x2B,
      0x06, 0x06, 0x02, 0x0F,
      10,                     //     10 ms delay
    ST7735_CASET  , 4      ,  // 15: Column addr set, 4 args, no delay:
      0x00, 0x02,             //     XSTART = 2
      0x00, 0x81,             //     XEND = 129
    ST7735_RASET  , 4      ,  // 16: Row addr set, 4 args, no delay:
      0x00, 0x02,             //     XSTART = 1
      0x00, 0x81,             //     XEND = 160
    ST7735_NORON  ,   DELAY,  // 17: Normal display on, no args, w/delay
      10,                     //     10 ms delay
    ST7735_DISPON ,   DELAY,  // 18: Main screen turn on, no args, w/delay
      255 };                  //     255 = 500 ms delay

static const uint8_t  Rcmd1[] = {                 // Init for 7735R, part 1 (red or green tab)
    15,                       // 15 commands in list:
    ST7735_SWRESET,   DELAY,  //  1: Software reset, 0 args, w/delay
      150,                    //     150 ms delay
    ST7735_SLPOUT ,   DELAY,  //  2: Out of sleep mode, 0 args, w/delay
      255,                    //     500 ms delay
    ST7735_FRMCTR1, 3      ,  //  3: Frame rate ctrl - normal mode, 3 args:
      0x01, 0x2C, 0x2D,       //     Rate = fosc/(1x2+40) * (LINE+2C+2D)
    ST7735_FRMCTR2, 3      ,  //  4: Frame rate control - idle mode, 3 args:
      0x01, 0x2C, 0x2D,       //     Rate = fosc/(1x2+40) * (LINE+2C+2D)
    ST7735_FRMCTR3, 6      ,  //  5: Frame rate ctrl - partial mode, 6 args:
      0x01, 0x2C, 0x2D,       //     Dot inversion mode
      0x01, 0x2C, 0x2D,       //     Line inversion mode
    ST7735_INVCTR , 1      ,  //  6: Display inversion ctrl, 1 arg, no delay:
      0x07,                   //     No inversion
    ST7735_PWCTR1 , 3      ,  //  7: Power control, 3 args, no delay:
      0xA2,
      0x02,                   //     -4.6V
      0x84,                   //     AUTO mode
    ST7735_PWCTR2 , 1      ,  //  8: Power control, 1 arg, no delay:
      0xC5,                   //     VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD
    ST7735_PWCTR3 , 2      ,  //  9: Power control, 2 args, no delay:
      0x0A,                   //     Opamp current small
      0x00,                   //     Boost frequency
    ST7735_PWCTR4 , 2      ,  // 10: Power control, 2 args, no delay:
      0x8A,                   //     BCLK/2, Opamp current small & Medium low
      0x2A,  
    ST7735_PWCTR5 , 2      ,  // 11: Power control, 2 args, no delay:
      0x8A, 0xEE,
    ST7735_VMCTR1 , 1      ,  // 12: Power control, 1 arg, no delay:
      0x0E,
    ST7735_INVOFF , 0      ,  // 13: Don't invert display, no args, no delay
    ST7735_MADCTL , 1      ,  // 14: Memory access control (directions), 1 arg:
      0xC0,                   //     row addr/col addr, bottom to top refresh, RGB order
    ST7735_COLMOD , 1+DELAY,  //  15: Set color mode, 1 arg + delay:
      0x05,                   //     16-bit color 5-6-5 color format
      10                      //     10 ms delay
};

static const uint8_t Rcmd2green[] = {            // Init for 7735R, part 2 (green tab only)
    2,                        //  2 commands in list:
    ST7735_CASET  , 4      ,  //  1: Column addr set, 4 args, no delay:
      0x00, 0x02,             //     XSTART = 0
      0x00, 0x7F+0x02,        //     XEND = 129
    ST7735_RASET  , 4      ,  //  2: Row addr set, 4 args, no delay:
      0x00, 0x01,             //     XSTART = 0
      0x00, 0x9F+0x01 };      //     XEND = 160

static const uint8_t Rcmd2red[] = {              // Init for 7735R, part 2 (red tab only)
    2,                        //  2 commands in list:
    ST7735_CASET  , 4      ,  //  1: Column addr set, 4 args, no delay:
      0x00, 0x00,             //     XSTART = 0
      0x00, 0x7F,             //     XEND = 127
    ST7735_RASET  , 4      ,  //  2: Row addr set, 4 args, no delay:
      0x00, 0x00,             //     XSTART = 0
      0x00, 0x9F };           //     XEND = 159

static const uint8_t Rcmd3[] = {                 // Init for 7735R, part 3 (red or green tab)
    4,                        //  4 commands in list:
    ST7735_GMCTRP1, 16      , //  1: Magical unicorn dust, 16 args, no delay:
      0x02, 0x1c, 0x07, 0x12,
      0x37, 0x32, 0x29, 0x2d,
      0x29, 0x25, 0x2B, 0x39,
      0x00, 0x01, 0x03, 0x10,
    ST7735_GMCTRN1, 16      , //  2: Sparkles and rainbows, 16 args, no delay:
      0x03, 0x1d, 0x07, 0x06,
      0x2E, 0x2C, 0x29, 0x2D,
      0x2E, 0x2E, 0x37, 0x3F,
      0x00, 0x00, 0x02, 0x10,
    ST7735_NORON  ,    DELAY, //  3: Normal display on, no args, w/delay
      10,                     //     10 ms delay
    ST7735_DISPON ,    DELAY, //  4: Main screen turn on, no args w/delay
      100 };                  //     100 ms delay

#define putpix(c) { lcd7735_senddata(c >> 8); lcd7735_senddata(c & 0xFF); }

static int colstart = 0;
static int rowstart = 0; // May be overridden in init func
//static uint8_t tabcolor	= 0;
static uint8_t orientation = PORTRAIT_NORMAL;
	  
// Companion code to the above tables.  Reads and issues
// a series of LCD commands stored in PROGMEM byte array.
static void commandList(const uint8_t *addr) {
  uint8_t  numCommands, numArgs;
  uint16_t ms;

  numCommands = *addr++;   // Number of commands to follow
  while(numCommands--) {                 // For each command...
    lcd7735_sendCmd(*addr++); //   Read, issue command
    numArgs  = *addr++;    //   Number of args to follow
    ms       = numArgs & DELAY;          //   If hibit set, delay follows args
    numArgs &= ~DELAY;                   //   Mask out delay bit
    while(numArgs--) {                   //   For each argument...
      lcd7735_sendData(*addr++);  //     Read, issue argument
    }

    if(ms) {
      ms = *addr++; // Read post-command delay time (ms)
      if(ms == 255) ms = 500;     // If 255, delay for 500 ms
      delay_ms(ms);
    }
  }
}

// Initialization code common to both 'B' and 'R' type displays
static void commonInit(const uint8_t *cmdList) {
  // toggle RST low to reset; CS low so it'll listen to us
	LCD_CS0;
	LCD_RST1;
	delay_ms(500);
	LCD_RST0;
    delay_ms(500);
	LCD_RST1;
	delay_ms(500);
    
	if(cmdList) commandList(cmdList);
}

// Initialization for ST7735B screens
void ST7735_initB(void) {
  commonInit(Bcmd);
}


// Initialization for ST7735R screens (green or red tabs)
void ST7735_initR(uint8_t options) {
  commonInit(Rcmd1);
  if(options == INITR_GREENTAB) {
    commandList(Rcmd2green);
    colstart = 2;
    rowstart = 1;
  } else {
    // colstart, rowstart left at default '0' values
    commandList(Rcmd2red);
  }
  commandList(Rcmd3);

  // if black, change MADCTL color filter
  if (options == INITR_BLACKTAB) {
    lcd7735_sendCmd(ST7735_MADCTL);
    lcd7735_sendData(0xC0);
  }

//  tabcolor = options;
}


void ST7735_setAddrWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {

  lcd7735_sendCmd(ST7735_CASET); // Column addr set
  lcd7735_sendData(0x00);
  lcd7735_sendData(x0+colstart);     // XSTART 
  lcd7735_sendData(0x00);
  lcd7735_sendData(x1+colstart);     // XEND

  lcd7735_sendCmd(ST7735_RASET); // Row addr set
  lcd7735_sendData(0x00);
  lcd7735_sendData(y0+rowstart);     // YSTART
  lcd7735_sendData(0x00);
  lcd7735_sendData(y1+rowstart);     // YEND

  lcd7735_sendCmd(ST7735_RAMWR); // write to RAM
}


void ST7735_pushColor(uint16_t color) {
  LCD_DC1;  
	putpix(color);
  //lcd7735_senddata(color >> 8);
  //lcd7735_senddata(color & 0xFF);
}

void ST7735_drawPixel(int16_t x, int16_t y, uint16_t color) {

  if((x < 0) ||(x >= _width) || (y < 0) || (y >= _height)) return;

  ST7735_setAddrWindow(x,y,x+1,y+1);
  ST7735_pushColor(color);
}

void ST7735_drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  // Rudimentary clipping
  if((x >= _width) || (y >= _height)) return;
  if((y+h-1) >= _height) h = _height-y;
  ST7735_setAddrWindow(x, y, x, y+h-1);

	LCD_DC1;
	while (h--) {
		putpix(color);
		//lcd7735_senddata(color >> 8);
		//lcd7735_senddata(color & 0xFF);
	}
}


void ST7735_drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
	// Rudimentary clipping
	if((x >= _width) || (y >= _height)) return;
	if((x+w-1) >= _width)  w = _width-x;
	ST7735_setAddrWindow(x, y, x+w-1, y);

	LCD_DC1;
	while (w--) {
		putpix(color);
		//lcd7735_senddata(color >> 8);
		//lcd7735_senddata(color & 0xFF);
	}
}

void ST7735_drawFastLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint16_t color) {
signed char   dx, dy, sx, sy;
unsigned char  x,  y, mdx, mdy, l;

  if (x1==x2) { // ������� ��������� ������������ �����
	  ST7735_fillRect(x1,y1, x1,y2, color);
	  return;
  }

  if (y1==y2) { // ������� ��������� �������������� �����
	  ST7735_fillRect(x1,y1, x2,y1, color);
	  return;
  }

  dx=x2-x1; dy=y2-y1;

  if (dx>=0) { mdx=dx; sx=1; } else { mdx=x1-x2; sx=-1; }
  if (dy>=0) { mdy=dy; sy=1; } else { mdy=y1-y2; sy=-1; }

  x=x1; y=y1;

  if (mdx>=mdy) {
     l=mdx;
     while (l>0) {
         if (dy>0) { y=y1+mdy*(x-x1)/mdx; }
            else { y=y1-mdy*(x-x1)/mdx; }
         ST7735_drawPixel(x,y,color);
         x=x+sx;
         l--;
     }
  } else {
     l=mdy;
     while (l>0) {
        if (dy>0) { x=x1+((mdx*(y-y1))/mdy); }
          else { x=x1+((mdx*(y1-y))/mdy); }
        ST7735_drawPixel(x,y,color);
        y=y+sy;
        l--;
     }
  }
  ST7735_drawPixel(x2, y2, color);
}

// ��������� �������������� (�� ������������)
void ST7735_drawRect(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2, uint16_t color) {
	ST7735_drawFastHLine(x1,y1,x2-x1, color);
	ST7735_drawFastVLine(x2,y1,y2-y1, color);
	ST7735_drawFastHLine(x1,y2,x2-x1, color);
	ST7735_drawFastVLine(x1,y1,y2-y1, color);
}

// fill a rectangle
void ST7735_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {	
  // rudimentary clipping (drawChar w/big text requires this)
  if((x >= _width) || (y >= _height)) return;
  if((x + w - 1) >= _width)  w = _width  - x;
  if((y + h - 1) >= _height) h = _height - y;

  ST7735_setAddrWindow(x, y, x+w-1, y+h-1);

  LCD_DC1;
  for(y=h; y>0; y--) {
    for(x=w; x>0; x--) {
		putpix(color);
		//lcd7735_senddata(color >> 8);
		//lcd7735_senddata(color & 0xFF);
    }
  }
}

void ST7735_fillScreen(uint16_t color) {
  ST7735_fillRect(0, 0,  _width, _height, color);
}

// Pass 8-bit (each) R,G,B, get back 16-bit packed color
uint16_t ST7735_Color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void ST7735_drawCircle(int16_t x, int16_t y, int radius, uint16_t color) {
	int f = 1 - radius;
	int ddF_x = 1;
	int ddF_y = -2 * radius;
	int x1 = 0;
	int y1 = radius;
 
	ST7735_setAddrWindow(x, y + radius, x, y + radius);
	ST7735_pushColor(color);
	ST7735_setAddrWindow(x, y - radius, x, y - radius);
	ST7735_pushColor(color);
	ST7735_setAddrWindow(x + radius, y, x + radius, y);
	ST7735_pushColor(color);
	ST7735_setAddrWindow(x - radius, y, x - radius, y);
	ST7735_pushColor(color);
	while(x1 < y1)
	{
		if(f >= 0) 
		{
			y1--;
			ddF_y += 2;
			f += ddF_y;
		}
		x1++;
		ddF_x += 2;
		f += ddF_x;    
		ST7735_setAddrWindow(x + x1, y + y1, x + x1, y + y1);
		ST7735_pushColor(color);
		ST7735_setAddrWindow(x - x1, y + y1, x - x1, y + y1);
		ST7735_pushColor(color);
		ST7735_setAddrWindow(x + x1, y - y1, x + x1, y - y1);
		ST7735_pushColor(color);
		ST7735_setAddrWindow(x - x1, y - y1, x - x1, y - y1);
		ST7735_pushColor(color);
		ST7735_setAddrWindow(x + y1, y + x1, x + y1, y + x1);
		ST7735_pushColor(color);
		ST7735_setAddrWindow(x - y1, y + x1, x - y1, y + x1);
		ST7735_pushColor(color);
		ST7735_setAddrWindow(x + y1, y - x1, x + y1, y - x1);
		ST7735_pushColor(color);
		ST7735_setAddrWindow(x - y1, y - x1, x - y1, y - x1);
		ST7735_pushColor(color);
	}
}

void ST7735_fillCircle(int16_t x, int16_t y, int radius, uint16_t color) {
	int x1,y1;
	for(y1=-radius; y1<=0; y1++) 
		for(x1=-radius; x1<=0; x1++)
			if(x1*x1+y1*y1 <= radius*radius) {
				ST7735_drawFastHLine(x+x1, y+y1, 2*(-x1), color);
				ST7735_drawFastHLine(x+x1, y-y1, 2*(-x1), color);
				break;
			}
}


void ST7735_drawBitmap(int x, int y, int sx, int sy, bitmapdatatype data, int scale) {
	int tx, ty, tc, tsx, tsy;

	if (scale==1) {
		if (orientation == PORTRAIT_NORMAL || orientation == PORTRAIT_FLIP)
		{
			ST7735_setAddrWindow(x, y, x+sx-1, y+sy-1);
			LCD_DC1;
			for (tc=0; tc<(sx*sy); tc++)
				putpix(data[tc]);
		} else {
			for (ty=0; ty<sy; ty++) {
				ST7735_setAddrWindow(x, y+ty, x+sx-1, y+ty);
				LCD_DC1;
				for (tx=sx-1; tx>=0; tx--)
					putpix(data[(ty*sx)+tx]);
			}
		}
	} else {
		if (orientation == PORTRAIT_NORMAL || orientation == PORTRAIT_FLIP) {
			for (ty=0; ty<sy; ty++) {
				ST7735_setAddrWindow(x, y+(ty*scale), x+((sx*scale)-1), y+(ty*scale)+scale);
				for (tsy=0; tsy<scale; tsy++)
					for (tx=0; tx<sx; tx++) {
						for (tsx=0; tsx<scale; tsx++)
							ST7735_pushColor(data[(ty*sx)+tx]);
					}
			}
		} else {
			for (ty=0; ty<sy; ty++) {
				for (tsy=0; tsy<scale; tsy++) {
					ST7735_setAddrWindow(x, y+(ty*scale)+tsy, x+((sx*scale)-1), y+(ty*scale)+tsy);
					for (tx=sx-1; tx>=0; tx--) {
						for (tsx=0; tsx<scale; tsx++)
							ST7735_pushColor(data[(ty*sx)+tx]);
					}
				}
			}
		}
	}
}

void ST7735_drawBitmapRotate(int x, int y, int sx, int sy, bitmapdatatype data, int deg, int rox, int roy) {
	int tx, ty, newx, newy;
	double radian;
	radian=deg*0.0175;  

	if (deg==0)
		ST7735_drawBitmap(x, y, sx, sy, data, 1);
	else
	{
		for (ty=0; ty<sy; ty++)
			for (tx=0; tx<sx; tx++) {
				newx=x+rox+(((tx-rox)*cos(radian))-((ty-roy)*sin(radian)));
				newy=y+roy+(((ty-roy)*cos(radian))+((tx-rox)*sin(radian)));

				ST7735_setAddrWindow(newx, newy, newx, newy);
				ST7735_pushColor(data[(ty*sx)+tx]);
			}
	}
}

void ST7735_setRotation(uint8_t m) {
  uint8_t rotation = m % 4; // can't be higher than 3

  lcd7735_sendCmd(ST7735_MADCTL);
  switch (rotation) {
   case PORTRAIT_NORMAL:
     lcd7735_sendData(MADCTL_MX | MADCTL_MY | MADCTL_RGB);
     _width  = ST7735_TFTWIDTH;
     _height = ST7735_TFTHEIGHT;
     break;
   case LANDSAPE_NORMAL:
     lcd7735_sendData(MADCTL_MY | MADCTL_MV | MADCTL_RGB);
     _width  = ST7735_TFTHEIGHT;
     _height = ST7735_TFTWIDTH;
     break;
  case PORTRAIT_FLIP:
     lcd7735_sendData(MADCTL_RGB);
     _width  = ST7735_TFTWIDTH;
     _height = ST7735_TFTHEIGHT;
    break;
   case LANDSAPE_FLIP:
     lcd7735_sendData(MADCTL_MX | MADCTL_MV | MADCTL_RGB);
     _width  = ST7735_TFTHEIGHT;
     _height = ST7735_TFTWIDTH;
     break;
   default:
	   return;
  }
  orientation = m;
}

static struct _font {
	uint8_t 	*font;
	uint8_t 	x_size;
	uint8_t 	y_size;
	uint8_t		offset;
	uint16_t	numchars;
} cfont;

void ST7735_setFont(uint8_t* font) {
	cfont.font=font;
	cfont.x_size=font[0];
	cfont.y_size=font[1];
	cfont.offset=font[2];
	cfont.numchars=font[3];
}

static uint8_t _transparent = 1;
static uint16_t _fg = ST7735_CYAN;
static uint16_t _bg = ST7735_BLACK;

void ST7735_setTransparent(uint8_t s) {
	_transparent = s;
}

void ST7735_setForeground(uint16_t s) {
	_fg = s;
}
void ST7735_setBackground(uint16_t s) {
	_bg = s;
}

void printChar(uint8_t c, int x, int y) {
	uint8_t i,ch,fz;
	uint16_t j;
	uint16_t temp; 
	int zz;

	if( cfont.x_size < 8 ) 
		fz = cfont.x_size;
	else
		fz = cfont.x_size/8;
	if (!_transparent) {
			ST7735_setAddrWindow(x,y,x+cfont.x_size-1,y+cfont.y_size-1);
	  
			temp=((c-cfont.offset)*((fz)*cfont.y_size))+4;
			for(j=0;j<((fz)*cfont.y_size);j++) {
				ch = cfont.font[temp];
				for(i=0;i<8;i++) {   
					if((ch&(1<<(7-i)))!=0)   
					{
						ST7735_pushColor(_fg);
					} 
					else
					{
						ST7735_pushColor(_bg);
					}   
				}
				temp++;
			}
	} else {
		temp=((c-cfont.offset)*((fz)*cfont.y_size))+4;
		for(j=0;j<cfont.y_size;j++) 
		{
			for (zz=0; zz<(fz); zz++)
			{
				ch = cfont.font[temp+zz]; 
				for(i=0;i<8;i++)
				{   
					ST7735_setAddrWindow(x+i+(zz*8),y+j,x+i+(zz*8)+1,y+j+1);
				
					if((ch&(1<<(7-i)))!=0)   
					{
						ST7735_pushColor(_fg);
					} 
				}
			}
			temp+=(fz);
		}
	}
}

void rotateChar(uint8_t c, int x, int y, int pos, int deg) {
	uint8_t i,j,ch,fz;
	uint16_t temp; 
	int newx,newy;
	double radian = deg*0.0175;
	int zz;

	if( cfont.x_size < 8 ) 
		fz = cfont.x_size;
	else
		fz = cfont.x_size/8;	
	temp=((c-cfont.offset)*((fz)*cfont.y_size))+4;
	for(j=0;j<cfont.y_size;j++) {
		for (zz=0; zz<(fz); zz++) {
			ch = cfont.font[temp+zz]; 
			for(i=0;i<8;i++) {   
				newx=x+(((i+(zz*8)+(pos*cfont.x_size))*cos(radian))-((j)*sin(radian)));
				newy=y+(((j)*cos(radian))+((i+(zz*8)+(pos*cfont.x_size))*sin(radian)));

				ST7735_setAddrWindow(newx,newy,newx+1,newy+1);
				
				if((ch&(1<<(7-i)))!=0) {
					ST7735_pushColor(_fg);
				} else  {
					if (!_transparent)
						ST7735_pushColor(_bg);
				}   
			}
		}
		temp+=(fz);
	}
}

void ST7735_print(char *st, int x, int y, int deg) {
	int stl, i;

	stl = strlen(st);

	if (x==RIGHT)
		x=(_width+1)-(stl*cfont.x_size);
	if (x==CENTER)
		x=((_height+1)-(stl*cfont.x_size))/2;

	for (i=0; i<stl; i++)
		if (deg==0)
			printChar(*st++, x + (i*(cfont.x_size)), y);
		else
			rotateChar(*st++, x, y, i, deg);
}


void ST7735_invertDisplay(const uint8_t mode) {
	if( mode == INVERT_ON ) lcd7735_sendCmd(ST7735_INVON);
	else if( mode == INVERT_OFF ) lcd7735_sendCmd(ST7735_INVOFF);
}

void ST7735_lcdOff() {
	lcd7735_sendCmd(ST7735_DISPOFF);
}

void St7735_lcdOn() {
	lcd7735_sendCmd(ST7735_DISPON);
}

/*
// ---------------------------------------------------------------------------------------------------------------------------------
// ��������� ���������� � ������
//
void lcd7735_init(void); // ������������� �������
		// ���������� ������������� ������� ������
void lcd7735_fillrect(unsigned char startX, unsigned char startY, unsigned char stopX, unsigned char stopY, unsigned int color);
		// ����� �������
void lcd7735_putpix(unsigned char x, unsigned char y, unsigned int Color);
		// ����� �����
void lcd7735_line(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2, unsigned int color);
		// ��������� �������������� (�� ������������)
void lcd7735_rect(char x1,char y1,char x2,char y2, unsigned int color);
		// ����� ������� � ����� �� �����������
void lcd7735_putchar(unsigned char x, unsigned char y, unsigned char chr, unsigned int charColor, unsigned int bkgColor);
		// ����� ������ � ����� �� �����������
void lcd7735_putstr(unsigned char x, unsigned char y, const unsigned char str[], unsigned int charColor, unsigned int bkgColor);
		// ������ ����������� �����
void LCD7735_dec(unsigned int numb, unsigned char dcount, unsigned char x, unsigned char y,unsigned int fntColor, unsigned int bkgColor);
// ��������� ���������� ������������� ������� ������ �������� ������


// ������ ����������� �����
void LCD7735_dec(unsigned int numb, unsigned char dcount, unsigned char x, unsigned char y,unsigned int fntColor, unsigned int bkgColor) {
	unsigned int divid=10000;
	unsigned char i;

	for (i=5; i!=0; i--) {

		unsigned char res=numb/divid;

		if (i<=dcount) {
			lcd7735_putchar(x, y, res+'0', fntColor, bkgColor);
			y=y+6;
		}

		numb%=divid;
		divid/=10;
	}
}



// ����� ������� �� ����� �� �����������
void lcd7735_putchar(unsigned char x, unsigned char y, unsigned char chr, unsigned int charColor, unsigned int bkgColor) {
	unsigned char i;
	unsigned char j;
#ifdef LCD_SEL_AUTO
	LCD_CS0;
#endif

	lcd7735_at(x, y, x+12, y+8);
	lcd7735_sendCmd(0x2C);

#ifdef LCD_TO_SPI2
	SPI2->CR1 |= SPI_CR1_DFF;   // ���� � ��� SPI2 �� �������� ������ 16�� �������� ��������������������
	LCD_DC1;
#endif
  unsigned char k;
	for (i=0;i<7;i++)
		for (k=2;k>0;k--) {
		   unsigned char chl=NewBFontLAT[ ( (chr-0x20)*14 + i+ 7*(k-1)) ];
		   chl=chl<<2*(k-1); // ������ �������� ������� �������� �� 1 ������� ����� (������� ���� ����� �����)
		   unsigned char h;
		   if (k==2) h=6; else h=7; // � ������ �������� ������� ������ 6 ����� ������ 7
		   for (j=0;j<h;j++) {
			unsigned int color;
			if (chl & 0x80) color=charColor; else color=bkgColor;
			chl = chl<<1;
#ifndef LCD_TO_SPI2
			lcd7735_sendData((unsigned char)((color & 0xFF00)>>8));
			lcd7735_sendData((unsigned char) (color & 0x00FF));
#else
			while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);
			SPI_I2S_SendData(SPI2, color);
#endif
		}
	}
	// ������ ������ �� ������� ������ ������������ ����� ��� �������� ���������
	for (j=0;j<13;j++) {
#ifndef LCD_TO_SPI2
			lcd7735_sendData((unsigned char)((bkgColor & 0xFF00)>>8));
			lcd7735_sendData((unsigned char) (bkgColor & 0x00FF));
#else
			while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);
			SPI_I2S_SendData(SPI2, bkgColor);
#endif

	}

#ifdef LCD_TO_SPI2
	while(SPI2->SR & SPI_SR_BSY);
	SPI2->CR1 &=~ SPI_CR1_DFF;   // ����������� � 8�� ������ �����
#endif
#ifdef LCD_SEL_AUTO
    LCD_CS1;
#endif
}



// ����� ������
void lcd7735_putstr(unsigned char x, unsigned char y, const unsigned char str[], unsigned int charColor, unsigned int bkgColor) {

	while (*str!=0) {
		lcd7735_putchar(x, y, *str, charColor, bkgColor);
		y=y+8;
		str++;
	}
}

*/