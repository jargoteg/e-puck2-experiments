#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ch.h"
#include "hal.h"
#include "memory_protection.h"
#include <main.h>
#include "leds.h"
#include "spi_comm.h"
#include "selector.h"
#include "motors.h"
#include "sensors/proximity.h"
#include "sensors/VL53L0X/VL53L0X.h"
#include "epuck1x/uart/e_uart_char.h"
#include "serial_comm.h"

messagebus_t bus;
MUTEX_DECL(bus_lock);
CONDVAR_DECL(bus_condvar);

int sensor_data[8], front_sensor;
int last[8];
float left_v=0, right_v=0,lastv,lastr;
float lmt = 100,lmt2 = 70,lmt3=25,sdlmt=200,sdlmt2=250;
float frntlmt = 200;
float pi=3.1416,error_x=0,diferror;

//Modes of operation
void mode_1(void);
void mode_2(void);

//Functions mode 1
void search(void);    //Move around to explore
void rotate(void);    //Rotate
void rot_dir(void);
void controller(void);

//Functions mode 2
void lock_front(void);
void right_side(void);
void left_side(void);
void search2(void);

//Globals mode1 
int sgn=1, dir=1, random_dir=1, searchl=500, searchr=600,extra=0,rot_spd=300,cont=0,vellow =321 ,velhigh =481; //vlow =321
float k = 75;
int mem =0;
//Globals mode2
int rot_spd2=350,ref_midpoint = 0,searchl2=500,searchr2=350;
float kp=1,ki=0;//0.28 prv
float acc_errorlef,acc_errorrig,errorlef,errorrig;
float error1=0,error2=0,acc_errorlef=0,acc_errorrig=0;


//////
// separate more from sensor[5] when rotating so the trajectoryu separates more from the wall.
// Should be less sensible of case when entering to paralel walls. It shouldnt start rotating.
//Work in something so that it completes the U trajectory not just one sector of the map.
// Maybe try bigger kp

int main(void)
{
  ref_midpoint =300; //could be 175
  //Initialize default
  messagebus_init(&bus, &bus_lock, &bus_condvar);
  halInit();
  chSysInit();
  mpu_init();
  motors_init();
  clear_leds();
  spi_comm_start();

  proximity_start();
  calibrate_ir();
  VL53L0X_start();


  serial_start();

  //int counter = 0;

  //int sensor_data[8];
    /* Infinite loop. */
    while (1) {
      //waits 1 second
      int selc = get_selector();
      //front_sensor = VL53L0x_get_dist_mm();
        
      // Obtain proximity sensor reading
      for(int i=0; i<8;i++)
      {
        sensor_data[i] = get_calibrated_prox(i);
      }
/*
      //Filter sensor data
      for(int i=0; i<8;i++)
      {
        sensor_data[i] = (sensor_data[i] + last[i])/2;
      }

      //Update last value
      for(int i=0; i<8;i++)
      {
        last[i] = sensor_data[i];
      }*/


      //Mode selector
      switch(selc)
      {
        case(1): //Explore mode
      {
        mode_1();
        break;
      }
        case(2): //Following mode
      {
        mode_2();
        break;
      }
      default:
        left_v = 0;
        right_v = 0;

        char str[100];
        int str_length=sprintf(str, "Printing data se %d\n",sensor_data[0]);
        set_led(LED1,2);
        e_send_uart1_char(str, str_length); //Send data using bluetooth
        chThdSleepMilliseconds(1000);
        break;
      }

      char str[100];
	  int str_length=sprintf(str, "Printing error %f\n",diferror);
	  //set_led(LED1,2);
	  e_send_uart1_char(str, str_length); //Send data using bluetooth

	 /* left_v = (left_v + lastv)/2;
	  right_v = (right_v + lastr)/2;

	  lastv = left_v;
	  lastr = right_v;*/

      left_motor_set_speed(left_v);
      right_motor_set_speed(right_v);
      
      /*if (counter < selc)
      {
      set_body_led(2);
      set_led(LED1,2);
      //set_led(LED3,2);
      set_led(LED7,2);
      left_motor_set_speed(left_v);
      right_motor_set_speed(right_v);
      counter++;
      chThdSleepMilliseconds(1000);
      continue;
      }
      left_v = left_v*-1;
      right_v = right_v*-1;
      counter = 0;*/
    }
    return 0;
}

#define STACK_CHK_GUARD 0xe2dee396
uintptr_t __stack_chk_guard = STACK_CHK_GUARD;

void __stack_chk_fail(void)
{
    chSysHalt("Stack smashing detected");
}


//Mode 1: Explore
void mode_1()
{
	//Wall detected, move away
    if (sensor_data[0]>lmt || sensor_data[7]>lmt){
        rot_dir();
        rotate();
    }else{
        controller();
    }
}


//Mode 2: Follow

void mode_2()
{
  
  search2();
	//Found something in the front
	if (sensor_data[0]>lmt2 || sensor_data[7]>lmt2)
	{
	  lock_front();
	}
  //Found something by right side
  if (sensor_data[1]>lmt2 || sensor_data[2]>lmt2)
  {
    right_side();
  }      
	//Found something by left side
	if (sensor_data[5]>lmt2 || sensor_data[6]>lmt2)
	{
	  left_side();
	}
	//Found something in the back
	if (sensor_data[4]>lmt2 || sensor_data[3]>lmt2)
	{
	  right_side();
	}

}

////////////////////////////////////////////
//Functions mode 1
void search()    //Move around to explore
{
  //extra = extra + 0.01;
  left_v = searchl;
  right_v = searchr;
}
void rotate()    //Rotate
{
    left_v = -rot_spd*dir;
    right_v = rot_spd*dir;
    mem = 0;
}

void controller()
{
	error_x = sensor_data[2] - sensor_data[5] + cos(45*pi/180)*(sensor_data[1]-sensor_data[6]) + cos(60*pi/180)*(sensor_data[3]-sensor_data[4]);

	left_v = vellow;
	right_v = vellow;
	mem = 1;
	if(error_x > 0){
		left_v = vellow;
		right_v = velhigh;
    }

    if(error_x < 0){
        left_v = velhigh;
        right_v = vellow;
    }
}

void rot_dir(){
    if (sensor_data[1]>lmt && mem == 1){
        dir = 1;
    }
    if(sensor_data[6]>lmt && mem == 1){
        dir = -1;
    }
}


///////////////////////////////////////////
//Functions mode 2
void lock_front()
{

	//PID controller
	//Error
	errorlef = -sensor_data[7] + ref_midpoint;
	errorrig = -sensor_data[0] + ref_midpoint;
	acc_errorlef = acc_errorlef + errorlef;
	acc_errorrig = acc_errorrig + errorrig;

	diferror = errorrig-errorlef;

	left_v = kp*errorlef + ki*acc_errorlef;
	right_v = kp*errorrig + ki*acc_errorrig;

	if (diferror > 200 && diferror<-200){
		//left_v = -200;
		//right_v = 200;

	}

	/*if(left_v < 50 && left_v > -50)
	{
		left_v=0;
	}
	if(right_v < 50 && right_v > -50)
	{
		right_v=0;
	}*/

}
void right_side()
{
  left_v = rot_spd2;
  right_v = -rot_spd2;
  acc_errorlef = 0;
  acc_errorrig = 0;
}

void left_side()
{
  left_v = -rot_spd2;
  right_v = rot_spd2;
  acc_errorlef = 0;
  acc_errorrig = 0;
}
void search2()
{
  left_v = searchl2;
  right_v = searchr2;
  acc_errorlef = 0;
  acc_errorrig = 0;
}

