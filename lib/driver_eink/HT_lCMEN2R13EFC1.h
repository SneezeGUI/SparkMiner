#ifndef __HT_ICMEN2R13EFC1_H__
#define __HT_ICMEN2R13EFC1_H__

#include <HT_Display.h>
#include <SPI.h>
#include <HT_lCMEN2R13EFC1_LUT.h>
SPIClass fSPI(HSPI);



class HT_ICMEN2R13EFC1 : public ScreenDisplay
{
private:
	uint8_t _rst;
	uint8_t _dc;
	int8_t _cs;
	int8_t _clk;
	int8_t _mosi;
	int8_t _miso;
	uint32_t _freq;
	uint8_t _spi_num;
	int8_t _busy;
	uint8_t _buf[4096];
	SPISettings _spiSettings;
	bool inverted;

public:
	uint8_t _width = 250;
	uint8_t _height = 122;

	HT_ICMEN2R13EFC1(uint8_t _rst, uint8_t _dc, int8_t _cs, int8_t _busy, int8_t _sck, int8_t _mosi, int8_t _miso, uint32_t _freq = 6000000, DISPLAY_GEOMETRY g = GEOMETRY_250_122)
	{
		setGeometry(g);
		this->_rst = _rst;
		this->_dc = _dc;
		this->_cs = _cs;
		this->_freq = _freq;
		this->_clk = _sck;
		this->_mosi = _mosi;
		this->_miso = _miso;
		this->_busy = _busy;
		this->displayType = E_INK;
		this->inverted = false;
	}

	bool connect()
	{
		pinMode(_dc, OUTPUT);
		pinMode(_rst, OUTPUT);
		pinMode(_cs, OUTPUT);
		digitalWrite(_cs, HIGH);
		pinMode(_busy, INPUT);
		this->buffer = _buf;
		fSPI.begin(this->_clk, this->_miso, this->_mosi);
		_spiSettings._clock = this->_freq;
		// Pulse Reset low for 10ms
		digitalWrite(_rst, HIGH);
		delay(100);
		digitalWrite(_rst, LOW);
		delay(100);
		digitalWrite(_rst, HIGH);
		return true;
	}

	void update(DISPLAY_BUFFER buffer) override
	{
		if (buffer == BLACK_BUFFER)
			// updateData(0x24);
			updateData(0x13);

		else if (buffer == COLOR_BUFFER)
			// updateData(0x26);
			updateData(0x13);
	}

	void setPartial()
	{
		sendCommand(0x92); // partial out

		WRITE_LUT_PARTIAL();

		sendCommand(0xE0);
		sendData(0X02);
		sendCommand(0xE5); // Refresh Speed or something
		sendData(0x75);
	}

	void setFast()
	{
		// will mess up partial mode - don't know how to revert it
		sendCommand(0x00);
		sendData(0xF7);
		WRITE_LUT_PARTIAL();
	}

	void setFull()
	{
		// may also mess up partial mode
		sendCommand(0x92); // partial out
		sendCommand(0x00);
		sendData(0xD7);
	}

	void refresh()
	{
		sendCommand(0x12); // Display Refresh
		delay(10);
		WaitUntilIdle();
	}

	void display()
	{
		sendCommand(0x04); // Power ON
		WaitUntilIdle();
		delay(10);

		refresh();

		sendCommand(0x02); // Power OFF
		WaitUntilIdle();
	}

	void setInverted(bool inverted)
	{
		this->inverted = inverted;
	}

	void updateData(uint8_t addr)
	{
		if (rotate_angle == ANGLE_0_DEGREE || rotate_angle == ANGLE_180_DEGREE)
		{
			int xmax = this->width();
			int ymax = this->height() >> 3;
			sendCommand(addr);
			digitalWrite(_cs, LOW);
			if (rotate_angle == ANGLE_0_DEGREE)
			{
				fSPI.beginTransaction(SPISettings(6000000, LSBFIRST, SPI_MODE0));
				for (int x = 0; x < 250; x++)
				{
					for (int y = 0; y < ymax; y++)
					{
						if (0x13 == addr)
						{
							if (true == inverted)
								fSPI.transfer(buffer[x + y * xmax]);
							else
								fSPI.transfer(~buffer[x + y * xmax]);
						}
						else
						{
							fSPI.transfer(buffer[x + y * xmax]);
						}
					}
				}
			}
			else
			{
				fSPI.beginTransaction(SPISettings(6000000, MSBFIRST, SPI_MODE0));
				for (int x = 250 - 1; x >= 0; x--)
				{
					for (int y = (ymax - 1); y >= 0; y--)
					{
						if (0x13 == addr)
						{
							if (y == 0)
								if (true == inverted)
									fSPI.transfer((buffer[x + y * xmax] << 6));
								else
									fSPI.transfer(~(buffer[x + y * xmax] << 6));
							else
								if (true == inverted)
									fSPI.transfer(((buffer[x + y * xmax] << 6) | (buffer[x + (y - 1) * xmax] >> 2)));
								else
									fSPI.transfer(~((buffer[x + y * xmax] << 6) | (buffer[x + (y - 1) * xmax] >> 2)));
						}
						else
						{
							if (y == 0)
								fSPI.transfer(buffer[x + y * xmax] << 6);
							else
								fSPI.transfer((buffer[x + y * xmax] << 6) | (buffer[x + (y - 1) * xmax] >> 2));
						}
					}
				}
			}
			digitalWrite(_cs, HIGH);
			fSPI.endTransaction();
		}
		else
		{
			uint8_t buffer_rotate[displayBufferSize];
			memset(buffer_rotate, 0, displayBufferSize);
			uint8_t temp;
			for (uint16_t i = 0; i < this->width(); i++)
			{
				for (uint16_t j = 0; j < this->height(); j++)
				{
					temp = buffer[(j >> 3) * this->width() + i] >> (j & 7) & 0x01;
					buffer_rotate[(i >> 3) * this->height() + j] |= (temp << (i & 7));
				}
			}
			sendCommand(addr);
			digitalWrite(_cs, LOW);
			int xmax = this->height();
			int ymax = this->width() >> 3;
			if (rotate_angle == ANGLE_90_DEGREE)
			{
				fSPI.beginTransaction(SPISettings(6000000, MSBFIRST, SPI_MODE0));
				for (int x = 0; x < 250; x++)
				{
					for (int y = (ymax - 1); y >= 0; y--)
					{
						if (0x10 == addr)
						{
							if (y == 0)
								if (true == inverted)
									fSPI.transfer((buffer_rotate[x + y * xmax] << 6));
								else
									fSPI.transfer(~(buffer_rotate[x + y * xmax] << 6));
							else
								if (true == inverted)
									fSPI.transfer(((buffer_rotate[x + y * xmax] << 6) | (buffer_rotate[x + (y - 1) * xmax] >> 2)));
								else
									fSPI.transfer(~((buffer_rotate[x + y * xmax] << 6) | (buffer_rotate[x + (y - 1) * xmax] >> 2)));
						}
						else
						{
							if (y == 0)
								fSPI.transfer(buffer_rotate[x + y * xmax] << 6);
							else
								fSPI.transfer((buffer_rotate[x + y * xmax] << 6) | (buffer_rotate[x + (y - 1) * xmax] >> 2));
						}
					}	
				}
			}
			else
			{
				fSPI.beginTransaction(SPISettings(6000000, LSBFIRST, SPI_MODE0));
				for (int x = 250 - 1; x >= 0; x--)
				{
					for (int y = 0; y < ymax; y++)
					{
						if (0x13 == addr)
						{
							if (true == inverted)
								fSPI.transfer(buffer_rotate[x + y * xmax]);
							else
								fSPI.transfer(~buffer_rotate[x + y * xmax]);
						}
						else
						{
							fSPI.transfer(buffer_rotate[x + y * xmax]);
						}
					}
				}
			}
			fSPI.endTransaction();
			digitalWrite(_cs, HIGH);
		}
	}

	void stop()
	{
		end();
	}

	void WRITE_LUT_PARTIAL()
	{
		unsigned int count;

		WaitUntilIdle();
		sendCommand(0x20);
		for (count = 0; count < 42; count++)
		{
			sendData(LUT_VCOM[count]);
		}

		WaitUntilIdle();
		sendCommand(0x21); // W
		for (count = 0; count < 42; count++)
		{
			sendData(LUT_WW[count]);
		}

		WaitUntilIdle();
		sendCommand(0x22); // W
		for (count = 0; count < 42; count++)
		{
			sendData(LUT_BW[count]);
		}

		WaitUntilIdle();
		sendCommand(0x23); // W
		for (count = 0; count < 42; count++)
		{
			sendData(LUT_WB[count]);
		}

		sendCommand(0x24); // B
		for (count = 0; count < 42; count++)
		{
			sendData(LUT_BB[count]);
		}
	}

	// keeping this for refferences
	void INIT_JD79656_mcu()
	{ // FITI cmd.
		sendCommand(0x4D);
		sendData(0x55);
		// sendCommand(0xA9);
		// sendData(0x25);
		sendCommand(0xF3);
		sendData(0x0A);

		// datasheet user cmd.

		// User cmd.
		sendCommand(0x00); // PSR
		sendData(0xF7);	   // 黑白
		sendData(0x08);	   //

		sendCommand(0x01); // PWR
		sendData(0x03);	   // 内部升压
		sendData(0x01);	   // VGH/VGL voltage: 00=+/-20V ; 01=+/-19V ; 02=+/-18V; 03=+/-17V; 04=+/-16V
		sendData(0x3F);	   // VSH voltage: 2B=11V ; 30=12V ; 35=13V  3A=14V  3F=15
		sendData(0x3F);	   // VSL voltage: 2B=11V ; 30=12V ; 35=13V  3A=14V  3F=15
		sendData(0x13);	   // VSHR voltage: 0B=4.6v 0C=4.8V; 0D=5V ; 0E=5.2V; 0F=5.4V ; 10=5.6v ; 11=5.8V 12=6V; 13=6.2V; 14=6.4V ; 15=9.6V

		sendCommand(0x06); // Booster  47uH 2.2om
		sendData(0xC7);	   //
		sendData(0x27);	   //
		sendData(0x3E);	   //

		sendCommand(0x50);
		// sendData(0x57); //  border  B17    W  57//RED
		sendData(0x97); //  border  B17    W  97/黑白

		sendCommand(0x60); // TCON定时/计数器
		sendData(0x22);	   //

		sendCommand(0x61); // 128*250 TRES分辨率
		sendData(0x80);	   // 0x80=128
		sendData(0xFA);	   // 0xFA=250

		sendCommand(0x82); // vcom voltage: 10=-1.7  11=-1.8; 13=-2.0V ; 15=-2.2; 17=-2.4;
		// sendData(0x10);
		sendData(CMD_USER[3]);

		sendCommand(0x30); // PLL 扫描频率
		// sendData(0x1A);   //frame rate:09=50; 0B=60; 0D=70; 13=100 1A=150
		sendData(CMD_USER[4]);

		sendCommand(0xE3); // PWS
		sendData(0x88);

		sendCommand(0xF8);
		sendData(0x80);

		sendCommand(0xB3);
		sendData(0x42);

		sendCommand(0xB4);
		sendData(0x28);

		sendCommand(0xAA);
		sendData(0xB7);

		sendCommand(0xA8);
		sendData(0x3D);
	}

private:
	int getBufferOffset(void)
	{
		return 0;
	}

	void WaitUntilIdle()
	{
		while (!digitalRead(_busy))
		{ // LOW: idle, HIGH: busy
			delay(1);
		}
		delay(1);
	}

	inline void sendCommand(uint8_t com) __attribute__((always_inline))
	{
		digitalWrite(_dc, LOW);
		digitalWrite(_cs, LOW);
		fSPI.beginTransaction(_spiSettings);
		fSPI.transfer(com);
		fSPI.endTransaction();
		digitalWrite(_cs, HIGH);
		digitalWrite(_dc, HIGH);
	}

	void sendData(unsigned char data)
	{
		digitalWrite(this->_cs, LOW);
		fSPI.transfer(data);
		digitalWrite(this->_cs, HIGH);
	}

	void sendInitCommands(void)
	{
		WaitUntilIdle();
		sendCommand(0x12); // soft reset
		WaitUntilIdle();

		sendCommand(0x4D);
		sendData(0x55);
		sendData(0x00);
		sendData(0x00);
		sendCommand(0xA9);
		sendData(0x25);
		sendData(0x00);
		sendData(0x00);
		sendCommand(0xF3);
		sendData(0x0A);
		sendData(0x00);
		sendData(0x00);
		// if (geometry == GEOMETRY_RAWMODE)
		// 	return;
		// WaitUntilIdle();
		// sendCommand(0x12); // soft reset
		// WaitUntilIdle();

		// sendCommand(0x74); //set analog block control
		// sendData(0x54);
		// sendCommand(0x7E); //set digital block control
		// sendData(0x3B);

		// sendCommand(0x01); //Driver output control
		// sendData(0xF9);
		// sendData(0x00);
		// sendData(0x00);

		// sendCommand(0x11); //data entry mode
		// sendData(0x01);

		sendCommand(0x44); // set Ram-X address start/end position
		sendData(0x01);
		sendData(0x0f); // 0x0F-->(15+1)*8=128

		sendCommand(0x45); // set Ram-Y address start/end position
		sendData(0xF9);	   // 0xF9-->(249+1)=250
		sendData(0x00);
		sendData(0x00);
		sendData(0x00);

		sendCommand(0x3C); // BorderWavefrom
		sendData(0x01);

		sendCommand(0x18);
		sendData(0x80);

		sendCommand(0x4E); // set RAM x address count to 0;
		sendData(0x01);
		sendCommand(0x4F); // set RAM y address count to 0xF9-->(249+1)=250;
		sendData(0xF9);
		sendData(0x00);
		WaitUntilIdle();
	}

	void sendScreenRotateCommand()
	{
		// not implemented
	}
};

#endif
