/* main.c
 * Joao Pedro Carvao
 * Albert Chu 
 * Francois Mertil
 *
 * Source code for our final project in ECE 4760 
 * 
*/

#include "synth.h"

void __ISR(_TIMER_2_VECTOR, ipl2) Timer2Handler(void)
{
    // 74 cycles to get to this point from timer event
    mT2ClearIntFlag();
    // main DDS phase
    DAC_data = 0;
    num_keys_pressed = 0;
    
    int i;
    static int delay_index;
    static int dk_flag;
    
    dk_flag = 0;
    
    
    dk_interval++;
    if (dk_interval == 255) {
        dk_flag = 1;
        dk_interval = 0;
    }
    // FM synthesis
    for (i = 0; i < NUM_KEYS; i++) {
        if (ramp_flag[i])
        {
            dk_state_fm[i] = fm_depth; 
            dk_state_main[i] = onefix16; 
            attack_state_fm[i] = fm_depth; 
            attack_state_main[i] = onefix16; 
        }
    
         if (dk_flag) {
            dk_state_fm_temp = dk_state_fm[i];
            dk_state_main_temp = dk_state_main[i];
            attack_state_fm_temp = attack_state_fm[i];
            attack_state_main_temp = attack_state_main[i];
            dk_state_fm_temp = multfix16(dk_state_fm_temp, dk_fm) ;
            dk_state_main_temp = multfix16(dk_state_main_temp, dk_main) ;
            attack_state_fm_temp = multfix16(attack_state_fm_temp, attack_fm);
            attack_state_main_temp = multfix16(attack_state_main_temp, attack_main);
            env_fm[i] = multfix16(fm_depth-attack_state_fm_temp, dk_state_fm_temp) ;
            
            if (sustain && button_pressed_in[i]) {
                dk_state_main[i] = onefix16;
                dk_state_fm[i] = fm_depth;
            }
            else {
                dk_state_main[i] = dk_state_main_temp;
                dk_state_fm[i] = dk_state_fm_temp;
            }
            env_main[i] = multfix16(onefix16-attack_state_main_temp, dk_state_main_temp);
            attack_state_fm[i] = attack_state_fm_temp;
            attack_state_main[i] = attack_state_main_temp;
        }
        
   		if (button_pressed_in[i] || ramp_flag[i]==0) {
            phase_accum_FM[i] += phase_incr_FM[i];
            if (fm_on) {//toggled by button in pt ui
                phase_accum_main[i] += phase_incr_main[i] + 
                        multfix16(env_fm[i],sin_table[phase_accum_FM[i]>>24]);
            }
            else {
                phase_accum_main[i] += phase_incr_main[i];
            }
		    DAC_data += ((fix2int16(env_main[i]<<9))*
                    fix2int16(sin_table[phase_accum_main[i]>>24]))>>9;
		    num_keys_pressed++;
    	}
    }
    // normalize key presses
    if (num_keys_pressed) {
        DAC_data = DAC_data/num_keys_pressed;
    }
    
    if (flanger_on) {  // toggled by button in pt_ui
        delay_counter++;
        if (delay_counter == DELAY_RAMP_PERIOD) {
            current_flanger_delay += flange_flag;
            delay_counter = 0;
        }
        if (current_flanger_delay <= 0){
            flange_flag = 1;
        }
        if (current_flanger_delay >= MAX_FLANGER_SIZE/2) {
            flange_flag = -1; 
        }

        flange_counter++;
        // MAX_FLANGER_SIZE-1 to avoid negative index in keys_pressed[i]
        if (flange_counter > MAX_FLANGER_SIZE-1) {
            flange_counter = 0;
            delay_on=1;
        }
        flange_buffer[flange_counter] = DAC_data;
        delay_index = 0;
        if (delay_on){
            delay_index = flange_counter - current_flanger_delay;
            if (delay_index < 0) {
                delay_index += MAX_FLANGER_SIZE;
            }
        }
        delay_signal = flange_buffer[delay_index];
    }    
        
    int junk;
    // === Channel A =============
    // CS low to start transaction
    SPI_Mode16();
     mPORTBClearBits(BIT_4); // start transaction
    // write to spi2 
    WriteSPI2( DAC_config_chan_A | ((DAC_data+delay_signal)>>1)+2048);
    while (SPI2STATbits.SPIBUSY); // wait for end of transaction
    // CS high
    junk = ReadSPI2(); 
    mPORTBSetBits(BIT_4); // end transaction
}// end ISR TIMER2

static PT_THREAD (protothread_read_inputs(struct pt *pt))
{
	PT_BEGIN(pt);
    static int pressed_old[NUM_KEYS];
	while (1) {
		PT_YIELD_TIME_msec(1);
        int i;
		for (i=0; i < NUM_KEYS; i++) {
            pressed_old[i] = button_pressed_in[i];
            button_pressed_in[i] = button_pressed[i];
            ramp_flag[i] = 0;
			if (button_pressed_in[i]) {
				// ramp up if nothing was pressed before 
                // and now something is pressed, indicating a new sound
                if (!pressed_old[i]) {
                    ramp_flag[i] = 1;
                    //record time of press/release and which key was pressed/released
                    if (!repeat_mode_on) {
                        keypresses[keypress_count] = PT_GET_TIME();
                        keypress_ID[keypress_count] = i;
                        keypress_count++;
                    }
                }

			}
			else {                
                if (pressed_old[i]) {
                    //record time of press/release and which key was pressed/released
                    if (!repeat_mode_on) {                    
                        keypresses[keypress_count] = PT_GET_TIME();
                        keypress_ID[keypress_count] = i;
                        keypress_count++;
                    }
                }
			}
		}

	}
	PT_END(pt);
}


static PT_THREAD (protothread_read_button(struct pt *pt))
{
    PT_BEGIN(pt);
    static int pressed[NUM_KEYS];
    static int inputY;
    static int inputZ;
    static int input;
    start_spi2_critical_section;
    initPE();
    mPortYSetPinsIn(BIT_0 | BIT_1 | BIT_2 | BIT_3 | BIT_4 | BIT_5 | BIT_6);
    mPortYEnablePullUp(BIT_0 | BIT_1 | BIT_2 | BIT_3 | BIT_4 | BIT_5 | BIT_6);
    
    mPortZSetPinsIn(BIT_0 | BIT_1 | BIT_2 | BIT_3 | BIT_4 | BIT_5);
    mPortZEnablePullUp(BIT_0 | BIT_1 | BIT_2 | BIT_3 | BIT_4 | BIT_5);
    end_spi2_critical_section ;
        
    while (1) {
        PT_YIELD_TIME_msec(1);
        start_spi2_critical_section;
        inputY = readPE(GPIOY);
        inputZ = readPE(GPIOZ);
        end_spi2_critical_section;
        input = inputY<<6;
        input = input | inputZ;
        button_input = input;
        int i;
        for (i=0; i<NUM_KEYS;i++) {
            //ensure that important bit is least significant, then mask to make 
            //sure that entire number is 1 or 0
            button_pressed[i] = !((input>>i) & 1);
        }
    }
    PT_END(pt);
}


static PT_THREAD (protothread_repeat_buttons(struct pt *pt))
{
    PT_BEGIN(pt);
    static int yield_length;
    static int i;
    static int j;
    while (1) {
        for (j = 0; j < NUM_KEYS; j++) {
            button_pressed[j]=0;
        }
        PT_YIELD_TIME_msec(keypresses[0]-start_recording);
        for (i = 0; i < valid_size; i++) {
            yield_length = keypresses[i+1] - keypresses[i];
            if (!button_pressed[keypress_ID[i]]) {
                button_pressed[keypress_ID[i]] = 1; 
            }
            else {
                button_pressed[keypress_ID[i]] = 0;
            }
            if (i != valid_size-1) {
                PT_YIELD_TIME_msec(((int)yield_length*tempo));
            }
        }
    }
    PT_END(pt);
}


static PT_THREAD (protothread_freq_tune(struct pt *pt))
{
    PT_BEGIN(pt);
    char buffer[256];
    static float scale;
    static int adc_val;
    static int adc_freq;
    static float scale2;
    static int counter = 0;  // used to decrease rate of printing
    while (1) {
        PT_YIELD_TIME_msec(5); 
        // adc_val of 502 is 0 for freq modulation
        static int j;
        adc_val = ReadADC10(0);
        adc_freq = ReadADC10(1);
        scale = ((float)(1.5*(adc_freq-502))/1024.0)+1;
        scale2 = ((float)(adc_val-500)/524.0);
        fx = scale2;
        for (j = 0; j < NUM_KEYS; j++) {
            frequencies[j] = ((int) frequencies_set[j] * scale);
            phase_incr_main[j]  = frequencies[j]*two32/Fs;
            // fm synth 
            frequencies_FM[j] = fm_ratio*frequencies[j];
            phase_incr_FM[j]  = frequencies_FM[j]*two32/Fs;
        }
        counter++;
    }
    PT_END(pt);
}


static PT_THREAD (protothread_cycle_button(struct pt *pt))
{
	PT_BEGIN(pt);

	while (1) {	
		PT_YIELD_TIME_msec(30);
		if (!cycle_state) {
			if (cycle_pressed) {
				cycle_state = 1;
				mod_param++;
				if (mod_param == 4) {
					mod_param = 0;
				}
			}
		}
		else {
			if (!cycle_pressed) {
				cycle_state = 0;
			}
		}
	}
	PT_END(pt);
}


static PT_THREAD (protothread_enter_button(struct pt *pt))
{
	PT_BEGIN(pt);
	while (1) {
		PT_YIELD_TIME_msec(30);
		if (!enter_state) {
			if (enter_pressed) {
				enter_state = 1;
				switch (mod_param) {
					case MOD_FM:
                        fm_ratio = fx*3+1;
						break;
					case MOD_FLANGER:
                        DELAY_RAMP_PERIOD = fx*1000 + 100;
						break;
					case MOD_ATK:
                        attack_main = fx*.1 + 0;
						break;
					case MOD_DECAY:
                        dk_main = fx*.1 + .9;
						break;
					default:
                        break;
				}
			}
		}
        else {
            if (!enter_pressed) {
                enter_state=0;
            }
        }
	}
	PT_END(pt);
}

/* Thread that processes user inputs */
static PT_THREAD (protothread_ui(struct pt *pt))
{
    PT_BEGIN(pt);
    char buffer[256];
    while (1) {
        PT_YIELD_TIME_msec(30);
        // flanger button state machine ======================================= 
        if (!flange_state && flange_pressed) {  
            flange_state = 1;
            // toggle flanger_on 
            if (!flanger_on) flanger_on = 1;
            else flanger_on = 0;  
        }
        else if (!flange_pressed) {
                flange_state = 0;
        }
        
        // repeat button state machine ========================================
        if (!repeat_state && repeat_pressed) {
            repeat_state = 1;
            // toggle repeat_mode_on
            if (!repeat_mode_on) {
                repeat_mode_on = 1;
                keypresses[keypress_count] = PT_GET_TIME();
                keypress_ID[keypress_count] = -1; 
                valid_size = keypress_count;
            }
            else {
                repeat_mode_on = 0;
                start_recording = PT_GET_TIME();
                int i;
                for (i=0; i<=keypress_count; i++) {
                    keypresses[i] = 0;
                    keypress_ID[i] = 0;
                }
                keypress_count = 0;
            }
            // record
        }
        else if (repeat_state && !repeat_pressed) {
            repeat_state = 0;
        }
        
        // divide by 4 for visibility improvement
        modified_tempo= ReadADC10(2)>>3;
        tempo = ReadADC10(2)/512.0;  // scale to 0-2
        
        // pitch display setting
        modified_pitch = frequencies[1]>>3;
        // adc 2 test
        freq_adc = ReadADC10(1);
        
        // FM synth button state machine ======================================
        if(!fm_state) {
            if (fm_pressed) {  
                fm_state = 1;
                // toggle flanger_on 
                if (!fm_on) fm_on = 1;
                else fm_on = 0;  
            }
        }
        else if (!fm_pressed) {
                fm_state = 0;
        }
        
        // sustain button state machine =======================================
        if(!sus_state && sus_pressed) { 
            sus_state = 1;
            // toggle flanger_on 
            if (!sustain) sustain = 1;
            else sustain = 0;  
        }
        else if (!sus_pressed) {
                sus_state = 0;
        }
    }
    PT_END(pt);
}

/* Thread showing display for user interface */
static PT_THREAD (protothread_ui_print(struct pt *pt))
{
    PT_BEGIN(pt);
    char buffer[256];
    while (1) {
        // update display once every 500ms
        PT_YIELD_TIME_msec(500);
        // flanger print ======================================================
        tft_setCursor(1,40);
        tft_fillRoundRect(0, 40, 125, 10, 1, ILI9340_BLACK);
        tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
        if (flanger_on){
            sprintf(buffer, "Flanger: on, %d", flange_pressed);
        }
        else {
            sprintf(buffer, "Flanger: off, %d", flange_pressed );
        }
        tft_writeString(buffer);

        // repeat mode print ==================================================
        tft_fillRoundRect(0, 60, 200, 10, 1, ILI9340_BLACK);
        tft_setCursor(1,60);
        tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
        if (repeat_mode_on) {
            sprintf(buffer, "Repeat Mode: on, %d, %i, %i", repeat_pressed, 
                    keypresses[0], keypresses[1]);
        }
        else {
            sprintf(buffer, "Repeat Mode: off, %d, %i, %i", repeat_pressed, 
                    keypresses[0],keypresses[1]);
        }
        tft_writeString(buffer);
        
        // Tempo Display ======================================================
        tft_setCursor(1,100);
        tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
        sprintf(buffer, "Tempo:");
        tft_writeString(buffer);
        tft_fillRoundRect(75, 100, 130, 25, 1, ILI9340_BLACK);
        tft_fillRoundRect(75, 100, modified_tempo, 25, 1, ILI9340_RED );

        // Pitch Display ======================================================
        tft_setCursor(1,125);
        tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
        sprintf(buffer, "Pitch:");
        tft_writeString(buffer);
        tft_fillRoundRect(75, 125, 130, 25, 1, ILI9340_BLACK);
        tft_fillRoundRect(75, 125, modified_pitch, 25, 1, ILI9340_GREEN );

        // FM synth state display =============================================
        tft_fillRoundRect(0, 170, 125, 10, 1, ILI9340_BLACK);
        tft_setCursor(1,170);
        tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
        if (fm_on) sprintf(buffer, "FM Synth: on, %d", fm_pressed);
        else sprintf(buffer, "FM Synth: off, %d", fm_pressed);
        tft_writeString(buffer);
        
        // Sustain state display ==============================================
        tft_fillRoundRect(0, 180, 125, 10, 1, ILI9340_BLACK);
        tft_setCursor(1,180);
        tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
        if (sustain) sprintf(buffer, "Sustain: on, %d", sus_pressed);
        else sprintf(buffer, "Sustain: off, %d", sus_pressed);
        tft_writeString(buffer);
        
        // mod_param print ====================================================
        tft_fillRoundRect(0, 190, 500, 10, 1, ILI9340_BLACK);
        switch (mod_param) {
            case MOD_FM:
                tft_setCursor(1,190);
                tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
                sprintf(buffer, "Modify FM Synthesis, FM_PARAM = %3.2f",fx*3+1);
                tft_writeString(buffer);
                break;
            case MOD_FLANGER:
                tft_setCursor(1,190);
                tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
                sprintf(buffer, "Modify Flanger, FLANGER_PARAM = %4.1f", fx*1000+100);
                tft_writeString(buffer);
                break;
            case MOD_ATK:
                tft_setCursor(1,190);
                tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
                sprintf(buffer, "Modify Attack Time ATK_PARAM = %1.4f", fx*.1);
                tft_writeString(buffer);
                break;
            case MOD_DECAY:
                tft_setCursor(1,190);
                tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
                sprintf(buffer, "Modify Decay Time DK_PARAM = %1.4f", fx*.1+.9);
                tft_writeString(buffer);
                break;
            default:
                tft_setCursor(1,190);
                tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
                sprintf(buffer, "mod_param error");
                tft_writeString(buffer);
                break;      
        }
        // Cycle Button Display ===============================================
        tft_fillRoundRect(0, 200, 125, 10, 1, ILI9340_BLACK);
        tft_setCursor(1,200);
        tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
        sprintf(buffer, "cycle_pressed: %d", cycle_pressed);
        tft_writeString(buffer);
        // Enter Button Display ===============================================
        tft_fillRoundRect(0, 210, 125, 10, 1, ILI9340_BLACK);
        tft_setCursor(1,210);
        tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
        sprintf(buffer, "enter button: %d", enter_pressed);
        tft_writeString(buffer);
        // Triple ADC TEST:
        tft_fillRoundRect(0, 220, 200, 10, 1, ILI9340_BLACK);
        tft_setCursor(1,220);
        static int adc0_eff, adc1_fq, adc2_tp;
        adc0_eff = ReadADC10(0); adc1_fq = ReadADC10(1); adc2_tp = ReadADC10(2);
        tft_setTextColor(ILI9340_YELLOW);  tft_setTextSize(1);
        sprintf(buffer, "ADC0: %d, ADC1: %d, ADC2: %d", 
                adc0_eff, adc1_fq, adc2_tp);
        tft_writeString(buffer);
        }
    PT_END(pt);
}

/* Thread that Reads Mux */
static PT_THREAD (protothread_read_mux(struct pt *pt )) 
{
    PT_BEGIN(pt);
    char buffer[256];
    
    // UI Analog Mux Ports ====================================================
    mPORTBSetPinsDigitalOut(BIT_7);   // A
    mPORTBSetPinsDigitalOut(BIT_10);  // B
    mPORTBSetPinsDigitalOut(BIT_13);  // C
    
    EnablePullUpB(BIT_0);
    mPORTBSetPinsDigitalIn(BIT_8); // read mux   
    
    while (1) {
        // flanger Toggle =====================================================
        //ABC = 000;
        mPORTBClearBits(BIT_7 | BIT_10 | BIT_13);
        //yield necessary otherwise you use the select mask from select signal
        PT_YIELD_TIME_msec(10);
        flange_pressed = mPORTBReadBits(BIT_8);
        // FM Synth Toggle ====================================================
        // ABC = 100
        mPORTBClearBits(BIT_10 | BIT_13);
        mPORTBSetBits(BIT_7);
        PT_YIELD_TIME_msec(10);
        fm_pressed = mPORTBReadBits(BIT_8);
        // Sustain Toggle =====================================================
        // ABC = 010
        mPORTBClearBits(BIT_7 | BIT_13);
        mPORTBSetBits(BIT_10);
        PT_YIELD_TIME_msec(10);
        sus_pressed = mPORTBReadBits(BIT_8); 
        // Cycle Button =======================================================
        // ABC = 001 
        mPORTBClearBits(BIT_7 | BIT_10);
        mPORTBSetBits(BIT_13);
        PT_YIELD_TIME_msec(10);
		cycle_pressed = mPORTBReadBits(BIT_8);
        // Enter Button =======================================================
        // ABC = 101
        mPORTBClearBits(BIT_10);
        mPORTBSetBits(BIT_7 | BIT_13);
        PT_YIELD_TIME_msec(10);
		enter_pressed = mPORTBReadBits(BIT_8);
        // Repeat Button ======================================================
        // ABC = 011
        mPORTBClearBits(BIT_7);
        mPORTBSetBits(BIT_10 | BIT_13);
        PT_YIELD_TIME_msec(10);
		repeat_pressed = mPORTBReadBits(BIT_8);
    }
    PT_END(pt);
}

/* Configures Analog-to-Digital Converter */
void adc_config(void)
{
    CloseADC10(); // ensure the ADC is off before setting the configuration
    // define setup parameters for OpenADC10
    #define PARAM1  ADC_FORMAT_INTG16 | ADC_CLK_AUTO | ADC_AUTO_SAMPLING_ON 
    // define setup parameters for OpenADC10
    #define PARAM2  ADC_VREF_AVDD_AVSS | ADC_OFFSET_CAL_DISABLE \
            | ADC_SCAN_ON | ADC_SAMPLES_PER_INT_3 | ADC_ALT_BUF_OFF \
            | ADC_ALT_INPUT_OFF
    // Define setup parameters for OpenADC10
    #define PARAM3 ADC_CONV_CLK_PB | ADC_SAMPLE_TIME_15 | ADC_CONV_CLK_Tcy 
    // define setup parameters for OpenADC10
    #define PARAM4  ENABLE_AN0_ANA | ENABLE_AN1_ANA | ENABLE_AN5_ANA
    // define setup parameters for OpenADC10 -- skip for all except AN0,1,5
    #define PARAM5 SKIP_SCAN_AN2 | SKIP_SCAN_AN3 | SKIP_SCAN_AN4 \
            | SKIP_SCAN_AN6 | SKIP_SCAN_AN7 | SKIP_SCAN_AN8 | SKIP_SCAN_AN9 \
            | SKIP_SCAN_AN10 | SKIP_SCAN_AN11 | SKIP_SCAN_AN12 \
            | SKIP_SCAN_AN13 | SKIP_SCAN_AN14 | SKIP_SCAN_AN15
    // configure to sample AN0, AN1, and AN5 on MUX A
    SetChanADC10(ADC_CH0_NEG_SAMPLEA_NVREF);
    // configure ADC 
    OpenADC10( PARAM1, PARAM2, PARAM3, PARAM4, PARAM5 ); 

    EnableADC10(); // Enable the ADC
}

/* Configures peripherals, timing, interrupts, and schedules+configures threads*/
void main(void)
{
    // Timing and ISR =========================================================
    // Set up timer2 on, interrupts, internal clock, prescalar 1:4, toggle rate
    OpenTimer2(T2_ON | T2_SOURCE_INT | T2_PS_1_4, 508);   
    ConfigIntTimer2(T2_INT_ON | T2_INT_PRIOR_2);
    mT2ClearIntFlag(); 
    
    // SPI + DAC setup ========================================================
    // SCK2 is pin 26 
    // SDO2 is in PPS output group 2, could be connected to RB5 which is pin 14
    PPSOutput(2, RPB5, SDO2);
    
    // config adc 
    adc_config();
    
    // control CS for DAC
    mPORTBSetPinsDigitalOut(BIT_4);
    mPORTBSetBits(BIT_4);
    // divide Fpb by 2, configure the I/O ports. Not using SS here
    // 16 bit transfer CKP=1 CKE=1
    // possibles SPI_OPEN_CKP_HIGH;   SPI_OPEN_SMP_END;  SPI_OPEN_CKE_REV
    // For any given peripheral, will need to match these
    
    SpiChnOpen(spiChn, SPI_OPEN_ON | SPI_OPEN_MODE16 | SPI_OPEN_MSTEN | 
            SPI_OPEN_CKE_REV , spiClkDiv);
    // Analog stuff
    ANSELA = 0; ANSELB = 0; CM1CON = 0; CM2CON = 0;
    
    // config threads =========================================================
    PT_setup();

    // setup system wide interrupts  ==========================================
    INTEnableSystemMultiVectoredInt();
    
    // TFT Setup ==============================================================
    // init the display
    tft_init_hw();
    tft_begin();
    tft_fillScreen(ILI9340_BLACK);
    //240x320 vertical display
    tft_setRotation(3);  // tft_setRotation(1) for 320x240
    
    // Sine Tables ============================================================
    // scaled to produce values between 0 and 4096
    int i;
    for (i = 0; i < SINE_TABLE_SIZE; i++){
        sin_table[i] =  
                float2fix16(2047*sin((float)i*6.283/(float)SINE_TABLE_SIZE));
    }
   
    static int j;
    for (j=0; j<KEYPRESS_SIZE; j++) {
       keypresses[j] = 0;
       keypress_ID[j] = 0;
    }

    static int k;
    for (k=0; k<MAX_FLANGER_SIZE; k++) {
        flange_buffer[k] = 0;
    }
    
    // PT INIT ================================================================
    PT_INIT(&pt_read_button);
    PT_INIT(&pt_read_inputs);
    PT_INIT(&pt_freq_tune);
    PT_INIT(&pt_repeat_buttons);
    PT_INIT(&pt_cycle_button);
    PT_INIT(&pt_enter_button);
    PT_INIT(&pt_ui);
    PT_INIT(&pt_ui_print);
    PT_INIT(&pt_read_mux);
    
    // scheduling loop 
    while(1) {
        if (!repeat_mode_on){
            PT_SCHEDULE(protothread_read_button(&pt_read_button));
        }
        else {
            PT_SCHEDULE(protothread_repeat_buttons(&pt_repeat_buttons));
        }
        PT_SCHEDULE(protothread_read_inputs(&pt_read_inputs));
        PT_SCHEDULE(protothread_freq_tune(&pt_freq_tune));
        PT_SCHEDULE(protothread_cycle_button(&pt_cycle_button));
        PT_SCHEDULE(protothread_enter_button(&pt_enter_button));
        PT_SCHEDULE(protothread_read_mux(&pt_read_mux));
        PT_SCHEDULE(protothread_ui(&pt_ui));
        PT_SCHEDULE(protothread_ui_print(&pt_ui_print));
    }    
}  // main
