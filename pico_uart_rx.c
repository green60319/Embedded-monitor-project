#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart0
#define BAUD_RATE 115200

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
//#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define SERVO_PIN 15
#define GREEN_LED_PIN 16
#define RED_LED_PIN 17

char command_buffer[32];
int command_index = 0;

void servo_pwm_init(void){

    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);

    uint slice_num = pwm_gpio_to_slice_num(SERVO_PIN);

    pwm_set_clkdiv(slice_num, 125.0f);
    pwm_set_wrap(slice_num, 20000 - 1);

    pwm_set_enabled(slice_num, true);
}

void servo_set_pulse_us(uint pulse_us){
    
    pwm_set_gpio_level(SERVO_PIN, pulse_us);
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);

    printf("Pico W Uart Receiver Ready\n");

    servo_pwm_init();
    servo_set_pulse_us(1500);

    gpio_init(GREEN_LED_PIN);
    gpio_set_dir(GREEN_LED_PIN, GPIO_OUT);

    gpio_init(RED_LED_PIN);
    gpio_set_dir(RED_LED_PIN, GPIO_OUT);

    gpio_put(GREEN_LED_PIN, 1);
    gpio_put(RED_LED_PIN, 0);

    // Set up our UART
    uart_init(UART_ID, BAUD_RATE);
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    //gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // Use some the various UART functions to send out data
    // In a default system, printf will also output via the default UART
    
    // Send out a string, with CR/LF conversions
    //uart_puts(UART_ID, " Hello, UART!\n");
    
    // For more examples of UART use see https://github.com/raspberrypi/pico-examples/tree/master/uart

    while (true) {
        if (uart_is_readable(UART_ID)){
            char ch = uart_getc(UART_ID);

            if(ch == '\n'){
                command_buffer[command_index] = '\0';

                printf("Received command: %s\n", command_buffer);

                if(strcmp(command_buffer, "SERVO_ON") == 0){
                    printf("Command recognized: SERVO_ON\n");
                    servo_set_pulse_us(1000);

                    gpio_put(GREEN_LED_PIN, 1);
                    gpio_put(RED_LED_PIN, 0);
                }
                else if(strcmp(command_buffer, "SERVO_OFF") == 0){
                    printf("Command recognized: SERVO_OFF\n");
                    servo_set_pulse_us(2000);
                    
                    gpio_put(GREEN_LED_PIN, 0);
                    gpio_put(RED_LED_PIN, 1);
                }
                else if(strcmp(command_buffer, "SERVO_IDLE") == 0){
                    printf("Command recognized: SERVO_IDLE\n");
                    servo_set_pulse_us(1500);
                }
                else{
                    printf("Unknown command\n");
                }

                command_index = 0; // Reset index for next command
            }
            else{
                if(command_index < sizeof(command_buffer) - 1){
                    command_buffer[command_index] = ch;
                    command_index++;
                }
            }
        }
    }
}
