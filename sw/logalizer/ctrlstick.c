/*
 * ctrlstick.c
 *
 * send joystick control to paparazzi through ivy 
 *
 * based on Force Feedback: Constant Force Stress Test
 * Copyright (C) 2001 Oliver Hamann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

//#define DBG 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <glib.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <Ivy/ivy.h>
#include <Ivy/ivyglibloop.h>

#ifdef DBG
#define dbgprintf fprintf
#else
#define dbgprintf(x ...)
#endif

#define TIMEOUT_PERIOD 100

#define MB_ID 1

#define INPUT_DEV_MAX   15

#define AXIS_COUNT      3

#define AXIS_RUDDER     ABS_X
#define AXIS_ELEVATOR   ABS_Y
#define AXIS_AILERON    ABS_Z
#define AXIS_THROTTLE   ABS_RZ

/* Helper for testing large bit masks */
#define TEST_BIT(bit,bits) (((bits[bit>>5]>>(bit&0x1f))&1)!=0)

/* Default values for the options */
#define DEVICE_NAME         "/dev/input/event"
#define DEFAULT_DEVICE_NAME "/dev/input/event4"
#define DEFAULT_AC_ID       1

/* Options */
const char * device_name_default = DEFAULT_DEVICE_NAME;
int aircraft_id                  = DEFAULT_AC_ID;

/* Global variables about the initialized device */
int device_handle;
int axis_code[AXIS_COUNT] = {AXIS_AILERON, AXIS_ELEVATOR, AXIS_THROTTLE};
int axis_min[AXIS_COUNT], axis_max[AXIS_COUNT];
int position[AXIS_COUNT] = {0, 0, 0};
struct ff_effect effect;

void parse_args(int argc, char * argv[])
{
	int i;

	if (argc<2) goto l_help;

	for (i=1; i<argc; i++) {
		if      (!strcmp(argv[i],"-d") && i<argc-1) device_name_default =argv[++i];
		else if (!strcmp(argv[i],"-a") && i<argc-1) aircraft_id         =atoi(argv[++i]);
		else if (!strcmp(argv[i],"-o")) ;
		else goto l_help;
	}
	return;

l_help:
	printf("Usage:\n");
	printf("  %s <option> [<option>...]\n",argv[0]);
	printf("Options:\n");
//	printf("  -d <string>  device name (default: %s)\n",DEFAULT_DEVICE_NAME);
	printf("  -a <int>     aircraft id (default: %d)\n",DEFAULT_AC_ID);
	printf("  -o           dummy option (useful because at least one option is needed)\n");
	exit(1);
}

int init_hid_device(char* device_name)
{
    int cnt;
	unsigned long key_bits[32],abs_bits[32];
//	unsigned long ff_bits[32];
	int valbuf[16];
	char name[256] = "Unknown";

	/* Open event device read only (with write permission for ff) */
	device_handle = open(device_name,O_RDONLY|O_NONBLOCK);
	if (device_handle<0) {
		dbgprintf(stderr,"ERROR: can not open %s (%s) [%s:%d]\n",
		        device_name,strerror(errno),__FILE__,__LINE__);
		return(1);
	}

	/* Which buttons has the device? */
	memset(key_bits,0,32*sizeof(unsigned long));
	if (ioctl(device_handle,EVIOCGBIT(EV_KEY,32*sizeof(unsigned long)),key_bits)<0) {
		dbgprintf(stderr,"ERROR: can not get key bits (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		return(1);
	}

	/* Which axes has the device? */
	memset(abs_bits,0,32*sizeof(unsigned long));
	if (ioctl(device_handle,EVIOCGBIT(EV_ABS,32*sizeof(unsigned long)),abs_bits)<0) {
		dbgprintf(stderr,"ERROR: can not get abs bits (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		return(1);
	}

	/* Which axis? */
	if  (!TEST_BIT(AXIS_RUDDER ,abs_bits)) {
		dbgprintf(stderr,"ERROR: no suitable rudder axis found [%s:%d]\n",
		        __FILE__,__LINE__);
		return(1);
	}
	if (!TEST_BIT(AXIS_ELEVATOR ,abs_bits)) {
		dbgprintf(stderr,"ERROR: no suitable elevator axis found [%s:%d]\n",
		        __FILE__,__LINE__);
		return(1);
	}
	if (!TEST_BIT(AXIS_AILERON ,abs_bits)) {
		dbgprintf(stderr,"ERROR: no suitable aileron axis found [%s:%d]\n",
		        __FILE__,__LINE__);
		return(1);
	}
	if (!TEST_BIT(AXIS_THROTTLE ,abs_bits)) {
		dbgprintf(stderr,"ERROR: no suitable throttle axis found [%s:%d]\n",
		        __FILE__,__LINE__);
		return(1);
	}
	
    for (cnt=0; cnt<AXIS_COUNT; cnt++)
    {
    	/* get axis value range */
    	if (ioctl(device_handle,EVIOCGABS(axis_code[cnt]),valbuf)<0) {
    		dbgprintf(stderr,"ERROR: can not get axis value range (%s) [%s:%d]\n",
    		        strerror(errno),__FILE__,__LINE__);
    		return(1);
    	}
    	axis_min[cnt]=valbuf[1];
    	axis_max[cnt]=valbuf[2];
    	if (axis_min[cnt]>=axis_max[cnt]) {
    		dbgprintf(stderr,"ERROR: bad axis value range (%d,%d) [%s:%d]\n",
    		        axis_min[cnt],axis_max[cnt],__FILE__,__LINE__);
    		return(1);
    	}
	}

#if 0
	/* Now get some information about force feedback */
	memset(ff_bits,0,32*sizeof(unsigned long));
	if (ioctl(device_handle,EVIOCGBIT(EV_FF ,32*sizeof(unsigned long)),ff_bits)<0) {
		dbgprintf(stderr,"ERROR: can not get ff bits (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		return(1);
	}

	/* force feedback supported? */
	if (!TEST_BIT(FF_CONSTANT,ff_bits)) {
		dbgprintf(stderr,"ERROR: device (or driver) has no force feedback support [%s:%d]\n",
		        __FILE__,__LINE__);
		return(1);
	}

	/* Switch off auto centering */
	memset(&event,0,sizeof(event));
	event.type=EV_FF;
	event.code=FF_AUTOCENTER;
	event.value=0;
	if (write(device_handle,&event,sizeof(event))!=sizeof(event)) {
		dbgprintf(stderr,"ERROR: failed to disable auto centering (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		return(1);
	}

	/* Initialize constant force effect */
	memset(&effect,0,sizeof(effect));
	effect.type=FF_CONSTANT;
	effect.id=-1;
	effect.trigger.button=0;
	effect.trigger.interval=0;
	effect.replay.length=0xffff;
	effect.replay.delay=0;
	effect.u.constant.level=0;
	effect.direction=0xC000;
	effect.u.constant.envelope.attack_length=0;
	effect.u.constant.envelope.attack_level=0;
	effect.u.constant.envelope.fade_length=0;
	effect.u.constant.envelope.fade_level=0;

	/* Upload effect */
	if (ioctl(device_handle,EVIOCSFF,&effect)==-1) {
		dbgprintf(stderr,"ERROR: uploading effect failed (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		return(1);
	}

	/* Start effect */
	memset(&event,0,sizeof(event));
	event.type=EV_FF;
	event.code=effect.id;
	event.value=1;
	if (write(device_handle,&event,sizeof(event))!=sizeof(event)) {
		dbgprintf(stderr,"ERROR: starting effect failed (%s) [%s:%d]\n",
		        strerror(errno),__FILE__,__LINE__);
		return(1);
	}
#endif	

	ioctl(device_handle, EVIOCGNAME(sizeof(name)), name);
	printf("Input device name: \"%s\"\n", name);

    return(0);
}

static gboolean joystick_periodic(gpointer data __attribute__ ((unused))) {

    int cnt;
    struct input_event event;

	/* Get events */
	while (read(device_handle,&event,sizeof(event))==sizeof(event)) {
	    for (cnt=0; cnt<AXIS_COUNT; cnt++) {
            if (event.type==EV_ABS && event.code==axis_code[cnt]) {
    			position[cnt]=(((event.value)-axis_min[cnt]))*254/(axis_max[cnt]-axis_min[cnt])-127;
	        }
		}
	}
    printf("pos  %d %d %d\n", position[0], position[1], position[2]);
	
	IvySendMsg("dl JOYSTICK_RAW %d %d %d %d", aircraft_id, position[0], position[1], position[2]);
//	IvySendMsg("dl DL_SETTING %d %d %f", aircraft_id, PPRZ_JOYSTICK_X, 0.1);

	return 1;
}

void on_DL_SETTING(IvyClientPtr app, void *user_data, int argc, char *argv[]){

  printf("%s\n", argv[0]);
  printf("%d\n", atoi(argv[1]));
  printf("%d\n", atoi(argv[2]));
  printf("%f\n", atof(argv[3]));

//  IvySendMsg("dl DL_SETTING %d %d %f", aircraft_id, PPRZ_JOYSTICK_X, 0.5);
}


int main ( int argc, char** argv) {

  GMainLoop *ml =  g_main_loop_new(NULL, FALSE);
  char devname[256];
  int cnt;
  
  parse_args(argc, argv);
  
  IvyInit ("IvyCtrlJoystick", "IvyCtrlJoystick READY", NULL, NULL, NULL, NULL);
  IvyBindMsg(on_DL_SETTING, NULL, "(\\S*) DL_SETTING (\\S*) (\\S*) (\\S*)");
  IvyStart("127.255.255.255");

  for (cnt=0; cnt<INPUT_DEV_MAX; cnt++){
    sprintf(devname, DEVICE_NAME "%d", cnt);
    if (init_hid_device(devname) == 0) break;
  }
  
  if (cnt == INPUT_DEV_MAX) {
    fprintf(stderr,"ERROR: no suitable joystick found [%s:%d]\n",
              __FILE__,__LINE__);
    return(1);
  }  
  
  g_timeout_add(TIMEOUT_PERIOD, joystick_periodic, NULL);
  
  g_main_loop_run(ml);

  return 0;
}
