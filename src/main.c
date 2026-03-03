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
unsigned int period    = 0;
unsigned int frequency = 0;

// Zero-crossing measurement (measure_zero_cross_time)
unsigned int zero_time_diff    = 0;
unsigned int time_us_prev      = 0;
unsigned int time_us_candidate = 0;
bit          ref_which_signal  = 0; // 0 = negative phase, 1 = positive

// Results written by amplitude() and update_phase_angle(), read by main()
float v1          = 0.0f; // Reference voltage RMS (P2.1)
float v2          = 0.0f; // Measured voltage RMS  (P1.6)
float phase_angle = 0.0f; // Phase angle in degrees

// Shared xdata formatting buffers (kept out of scarce internal DATA RAM)
static xdata char fmt_buf[20];  // used by amplitude() and update_phase_angle()
static xdata char uart_buf[32]; // used by main() for the data packet

// ---- Non-blocking phase monitor state ----
static bit phase_monitor_init = 0;
static bit last_p22 = 0;
static bit last_p23 = 0;
static bit period_wait_fall = 0;
static bit zero_wait_second = 0;
static unsigned int period_start_tick = 0;
static unsigned int zero_start_tick = 0;

static unsigned int timer0_read16(void)
{
    unsigned char tl = TL0;
    unsigned char th = TH0;
    return (((unsigned int)th) << 8) | tl;
}

static void phase_monitor_step(void)
{
    bit p22, p23, rise22, fall22, rise23;
    unsigned int now_ticks, delta_ticks, time_us;
    float angle;

    p22 = P2_2;
    p23 = P2_3;

    if (!phase_monitor_init) {
        last_p22 = p22;
        last_p23 = p23;
        phase_monitor_init = 1;
        return;
    }

    rise22 = (!last_p22) && p22;
    fall22 = last_p22 && (!p22);
    rise23 = (!last_p23) && p23;
    now_ticks = timer0_read16();

    // Keep the original period behavior: measure P2.2 high-time from rising->falling.
    if (!period_wait_fall) {
        if (rise22) {
            period_start_tick = now_ticks;
            period_wait_fall = 1;
        }
    } else if (fall22) {
        delta_ticks = now_ticks - period_start_tick;
        time_us = delta_ticks / 3U; // keep original scale used by existing code
        if (time_us >= 100U) {
            period = time_us;
            last_accept_frequency = time_us;
        } else if (last_accept_frequency != 0U) {
            period = last_accept_frequency;
        }
        if (period != 0U) {
            frequency = 1000000U / period;
        }
        period_wait_fall = 0;
    }

    // Non-blocking replacement for zero-cross wait loops.
    if (!zero_wait_second) {
        if ((!ref_which_signal && rise22) || (ref_which_signal && rise23)) {
            zero_start_tick = now_ticks;
            zero_wait_second = 1;
        }
    } else {
        if ((!ref_which_signal && rise23) || (ref_which_signal && rise22)) {
            delta_ticks = now_ticks - zero_start_tick;
            zero_time_diff = (delta_ticks / 3U) * 2U; // keep original scale
            zero_wait_second = 0;

            if (period != 0U) {
                if (ref_which_signal) {
                    angle = (float)zero_time_diff * (360.0f) / (float)period;
                    if (angle > 180.0f) angle -= 360.0f;
                } else {
                    angle = (float)zero_time_diff * (-360.0f) / (float)period;
                    if (angle <= -180.0f) angle += 360.0f;
                }

                if (ref_which_signal == 0) {
                    if (angle < 0.0f) {
                        phase_angle = angle;
                        sprintf(fmt_buf, "angle:%.2f\n", phase_angle);
                        UART_send_string(fmt_buf);
                    } else {
                        ref_which_signal = 1;
                    }
                } else {
                    if (angle >= 0.0f) {
                        phase_angle = angle;
                        sprintf(fmt_buf, "angle:%.2f\n", phase_angle);
                        UART_send_string(fmt_buf);
                    } else {
                        ref_which_signal = 0;
                    }
                }
            }
        }
    }

    last_p22 = p22;
    last_p23 = p23;
}

static void waitms_with_phase_monitor(unsigned int ms)
{
    unsigned int j;
    unsigned char k;

    for (j = 0; j < ms; j++) {
        for (k = 0; k < 100; k++) {
            phase_monitor_step();
        }
        waitms(1);
    }
}

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
        waitms_with_phase_monitor(1);
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

// ---- Measure amplitudes and update globals v1, v2 ----
void amplitude(void)
{
    v1 = read_ripple_voltage(QFP32_MUX_P2_1); // Reference voltage
    v2 = read_ripple_voltage(QFP32_MUX_P1_6);

    // Convert peak to RMS: Vrms = (Vpeak + Vd) / sqrt(2)
    v1 = (v1 + 0.2f) / 1.41421356237f;
    v2 = (v2 + 0.2f) / 1.41421356237f;

    sprintf(fmt_buf,"v1: %.2f\n", v1);
    UART_send_string(fmt_buf);
    sprintf(fmt_buf,"v2: %.2f\n", v2);
    UART_send_string(fmt_buf);
}

// ---- Measure phase angle and update global phase_angle ----
void update_phase_angle(void)
{
    phase_monitor_step();
}

// ---- Main ----
void main(void)
{
    char buff[24]; // LCD display buffer

    UART_init();
    UART1_init();

    waitms(500);
    UART_send_string("\x1b[2J"); // Clear screen
    UART_send_string("Lab5\nFile: ");
    UART_send_string(__FILE__);
    UART_send_string("\nCompiled: ");
    UART_send_string(__DATE__);
    UART_send_string(", ");
    UART_send_string(__TIME__);
    UART_send_string("\n\n");

    // Initialize ADC pins: P1.6 = v2, P2.1 = v1
    InitPinADC(1, 6);
    InitPinADC(2, 1);
    InitADC();
    TIMER0_Init();
    TR0 = 1; // Free-running timer source for non-blocking edge time stamps
    init_pin_input();
    LCD_4BIT();

    while (1) {
        amplitude();
        update_phase_angle();

        // LCD line 1: "Vr=x.xxv xxxHz"
        sprintf(buff, "Vr=%.2fv %uHz", v1, frequency);
        LCDprint(buff, 1, 1);

        // LCD line 2: "Vm=x.xxv xxx.x°"
        sprintf(buff, "Vm=%.2fv %.1f%c", v2, phase_angle, 0xDF);
        LCDprint(buff, 2, 1);

        // Send structured data packet for Python oscilloscope.
        // Format: $<v1>,<v2>,<freq>,<phase>\n
        // The '$' prefix lets the Python parser ignore other debug output.
        sprintf(uart_buf, "$%.3f,%.3f,%u,%.2f\n", v1, v2, frequency, phase_angle);
        UART_send_string(uart_buf);
		UART1_send_string("hello\n");

        waitms_with_phase_monitor(500);
    }
}
