#include <stdio.h>
#include <stdlib.h>
#include <EFM8LB1.h>
#include <string.h>
#include "config.h"
#include "bootloader.h"
#include "lcd.h"
#include "timer.h"
#include "uart.h"
#include "cmd.h"

// ---- Global state variables ----

// Period / frequency measurement (measure_period)
unsigned int last_accept_frequency = 0;

// Zero-crossing measurement (measure_zero_cross_time)
unsigned int zero_time_diff    = 0;
unsigned int time_us_prev      = 0;
unsigned int time_us_candidate = 0;
bit ref_which_signal = 0; // 0 = negative phase, 1 = positive
bit rms_vpp_signal = 0;

// Shared xdata formatting buffers (kept out of scarce internal DATA RAM)
static xdata char fmt_buf[20];  // used by amplitude() and update_phase_angle()
static xdata char uart_buf[32]; // used by main() for the data packet

// ---- Raw ADC read from a mux pin ----
unsigned int ADC_at_Pin(unsigned char pin)
{
	ADC0MX = pin;   // Select input from pin
	ADINT = 0;
	ADBUSY = 1;     // Convert voltage at the pin
	while (!ADINT); // Wait for conversion to complete
	return (ADC0);
}

// ---- Convert ADC pin reading to volts ----
float Volts_at_Pin(unsigned char pin)
{
	return (ADC_at_Pin(pin) * VDD) / 0b_0011_1111_1111_1111;
}

// ---- Measure peak voltage with ripple (average of 32 samples) ----
float read_ripple_voltage(unsigned char pin)
{
    unsigned char sample_count = 0;
    float max_voltage = (ADC_at_Pin(pin) * VDD) / 0b_0011_1111_1111_1111;
    float measured_voltage;

    while (sample_count < 32) {
        measured_voltage = (ADC_at_Pin(pin) * VDD) / 0b_0011_1111_1111_1111;
        if (measured_voltage > max_voltage) {
            max_voltage = measured_voltage;
        }
        sample_count++;
        waitms(1);
    }
    return max_voltage;
}

// ---- Measure signal period in microseconds (from P2.2) ----
unsigned int measure_period(void)
{
    unsigned int time_us;

    CKCON0 &= 0b_1111_1000; // Timer 0 uses sysclk/12
    TR0 = 0;
    TH0 = 0; TL0 = 0;       // Reset timer
    while (P2_2 == 1);       // Wait for signal to go low
    while (P2_2 == 0);       // Wait for signal to go high
    TR0 = 1;                 // Start timing
    while (P2_2 == 1);       // Wait for signal to go low
    TR0 = 0;                 // Stop timer

    time_us = ((unsigned int)(TH0 * 0x100 + TL0) / 3U);
    if (time_us < 100) {
        time_us = last_accept_frequency; // Reject glitches
    } else {
        last_accept_frequency = time_us;
    }
    return time_us;
}

// ---- Measure zero-crossing time difference between P2.2 and P2.3 ----
// P2.2 is the reference signal. Returns time difference in microseconds.
unsigned int measure_zero_cross_time(void)
{
    unsigned int time_us;
    int diff;

    CKCON0 &= 0b_1111_1010; // Timer 0 uses sysclk/48
    CKCON0 |= 0b_0000_0010;
    TR0 = 0;
    TH0 = 0; TL0 = 0;

    if (!ref_which_signal) {
        while (P2_2 == 1); // Wait for P2.2 low
        while (P2_2 == 0); // Wait for P2.2 high (rising edge)
        TR0 = 1;
        while (P2_3 == 1); // Wait for P2.3 low
        while (P2_3 == 0); // Wait for P2.3 high (rising edge)
        TR0 = 0;
    } else {
        while (P2_3 == 1); // Wait for P2.3 low
        while (P2_3 == 0); // Wait for P2.3 high (rising edge)
        TR0 = 1;
        while (P2_2 == 1); // Wait for P2.2 low
        while (P2_2 == 0); // Wait for P2.2 high (rising edge)
        TR0 = 0;
    }

    CKCON0 &= 0b_1111_1000; // Restore Timer 0 to sysclk/12
    time_us = ((unsigned int)(TH0 * 0x100 + TL0) / 3U) * 2U;

    if (time_us_prev == 0) {
        time_us_prev = time_us;
    } else {
        diff = time_us_prev - time_us;
        if (diff < 0) diff = -diff;

        if (diff <= 10) {
            time_us_candidate = 0;
            time_us_prev = time_us;
        } else {
            if (time_us_candidate != 0) {
                int cdiff = (int)time_us - (int)time_us_candidate;
                if (cdiff < 0) cdiff = -cdiff;
                if (cdiff <= 10) {
                    time_us_prev = time_us;
                    time_us_candidate = 0;
                } else {
                    time_us_candidate = time_us;
                    return time_us_candidate;
                }
            } else {
                time_us_candidate = time_us;
            }
        }
    }
    return time_us;
}


// ---- Main ----
void main(void){
    // Measured voltage variables
    float v1;
    float v2;
    unsigned int period;
    unsigned int frequency;
	unsigned int zero_time_diff;
	float phase_angle;
	char buff[16]; // LCD buffer
    xdata char cmd_buf[64];
    unsigned char cmd_len = 0;
    
    UART_init();
    UART1_init();
    waitms(500);

    // p1.6 is v2; p2.1 is v1
    InitPinADC(1, 6);
    InitPinADC(2, 1);
    InitADC();
    TIMER0_Init();
    init_pin_input();
	
	// Configure LCD
	LCD_4BIT();
    LCDprint("Welcome", 1, 1);
    UART_send_string("Welcome\n");
	UART1_send_string("Welcome\n");
	
    while(1){

        while (UART1_available()) {
			char c = UART1_read();
			if (c == '\r' || c == '\n') {
				if (cmd_len > 0) {
					cmd_buf[cmd_len] = '\0';
					UART1_send_string("\r\n");
					cli_parse_and_dispatch(cmd_buf);
					cmd_len = 0;
				}
			} else if (cmd_len < 31) {
				cmd_buf[cmd_len++] = c;
				UART1_send_char(c);  // echo
			}
		}

        v1 = read_ripple_voltage(QFP32_MUX_P2_1); // v1 is the reference voltage
        v2 = read_ripple_voltage(QFP32_MUX_P1_6);
		
		

        // calculate the rms value of v1, v2
        if (!rms_vpp_signal) {
		    // calculate the rms value of v1, v2
		// formula: vrms = (v_measure + vd)/sqrt(2)
            if(v1 > 1.48492424049){
                v1 = (v1) / 1.41421356237;
            }
            else{
                v1 = (v1 + 0.07) / 1.41421356237;
            }

            if(v2 > 1.48492424049){
                v2 = (v2) / 1.41421356237;
            }
            else{
                v2 = (v2 + 0.07) / 1.41421356237;
            }
        }
        else {
            if(v1 < 1){
                v1 = (v1 + 0.1) * 2;
            }
            else{
                v1 = (v1) * 2;
            } 

            if(v2 < 1){
                v2 = (v2 + 0.1) * 2;
            }
            else{
                v2 = v2 * 2;
            }
        }
        
		period = measure_period();
		if(period == 0){
			waitms(100);
			continue;
		}

        frequency = 1000000 / period; // frequency in Hz
        //printf("f2 = %u\n\n",frequency);

		zero_time_diff = measure_zero_cross_time();
		//phase_angle = (int)((long)zero_time_diff * (-1L * 360L) / (long)period);
		//printf("time diff: %u\n", zero_time_diff);
		//printf("period: %u\n", period);
		if (ref_which_signal) {
			phase_angle = (float)zero_time_diff * (360.0f) / (float)period;
			if (phase_angle > 180){
				phase_angle -= 360;
			}
		}
		else {
			phase_angle = (float)zero_time_diff * (-360.0f) / (float)period;
			if (phase_angle <= -180){
				phase_angle += 360;
			}
		}

		if (ref_which_signal == 0) { 
            if (phase_angle >= 0) {
				ref_which_signal = 1;
			}
		}
		else {
            if (phase_angle < 0) {
				ref_which_signal = 0;
			}
		}

		// Display format:
		// Vr = x.xv xHz
		// Vn = x.xv x (degree)
		sprintf(buff, "Vr=%.2fv %uHz", v1, frequency);
		LCDprint(buff, 1, 1);
		sprintf(buff, "Vm=%.2fv %.1f%c", v2, phase_angle, 0xDF);
		LCDprint(buff, 2, 1);

        // Send structured data packet for Python oscilloscope.
        // Format: $<v1>,<v2>,<freq>,<phase>\n
        // The '$' prefix lets the Python parser ignore other debug output.
        
        sprintf(uart_buf, "$%.3f,%.3f,%u,%.2f\n", v1, v2, frequency, phase_angle);
        UART_send_string(uart_buf);
        waitms(500);
    }
    
}

