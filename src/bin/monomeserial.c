/*
 * This file is part of libmonome.
 * libmonome is copyright 2007, 2008 will light <visinin@gmail.com>
 *
 * libmonome is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <getopt.h>
#include <lo/lo.h>

#include "monome.h"

#define DEFAULT_MONOME_DEVICE 	"/dev/ttyUSB0"
#define DEFAULT_MONOME_PROTOCOL "series"

#define DEFAULT_OSC_PREFIX		"monome"
#define DEFAULT_OSC_SERVER_PORT	"8080"
#define DEFAULT_OSC_APP_PORT	"8000"
#define DEFAULT_OSC_APP_HOST    NULL	/* liblo takes this to mean 'localhost' */

char *lo_prefix;
lo_address *outgoing;
lo_server_thread *st;

void lo_error(int num, const char *error_msg, const char *path) {
	printf("monomeserial: lo server error %d in %s: %s\n", num, path, error_msg);
	fflush(stdout);
}

int clear_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message data, void *user_data) {
	monome_t *monome = user_data;
	int mode = 0;

	if( argc > 0 )
		mode = argv[0]->i & 0x01;
	
	return monome_clear(monome, mode);
}

int led_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message data, void *user_data) {
	monome_t *monome = user_data;
	
	if( (argc != 3 || strcmp("iii", types)) ||
		(argv[0]->i > 15 || argv[0]->i < 0) ||
		(argv[1]->i > 15 || argv[1]->i < 0) ||
		(argv[2]->i > 1  || argv[2]->i < 0) )
		return -1;
	
	if( argv[2]->i )
		return monome_led_on(monome, argv[0]->i, argv[1]->i);
	else
		return monome_led_off(monome, argv[0]->i, argv[1]->i);
}

int led_col_row_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message data, void *user_data) {
	monome_t *monome = user_data;
	unsigned int buf[2];
	
	if( argv[0]->i > 15 || argv[0]->i < 0 )
		return -1;
	
	switch( argc ) {
	case 2:
		if( strncmp("ii", types, 2) )
			return -1;
		
		buf[0] = argv[1]->i;
		
		if( strstr(path, "led_col") )
			return monome_led_col_8(monome, argv[0]->i, buf);
		else
			return monome_led_row_8(monome, argv[0]->i, buf);
		
		break;
		
	case 3:
		if( strncmp("iii", types, 3) )
			return -1;

		buf[0] = argv[1]->i;
		buf[1] = argv[2]->i;
		
		if( strstr(path, "led_col") )
			return monome_led_col_16(monome, argv[0]->i, buf);
		else
			return monome_led_row_16(monome, argv[0]->i, buf);
		
		break;
	}
	
	return 0;
}

int frame_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message data, void *user_data) {
	monome_t *monome = user_data;
	unsigned int buf[8], i;

	for( i = 0; i < 8; i++ )
		buf[i] = argv[i]->i;
	
	switch( argc ) {
	case 8:
		return monome_led_frame(monome, 0, buf);
		break;
		
	case 9:
		return monome_led_frame(monome, argv[8]->i, buf);
		break;
		
	case 10:
		/**
		 * okay, this isn't implemented yet.
		 * passing 10 arguments to /frame means you want to offset it by argv[8] and argv[9]
		 * thing is, there's no clean mapping to the serial protocol
		 * so this is going to have to wait until v0.2 at the earliest.
		 */
		break;
	}
	
	return -1;
}

void handle_press(const monome_event_t *e, void *data) {
	char *cmd;
	char *prefix = data;

	asprintf(&cmd, "/%s/press", prefix);
	lo_send_from(outgoing, lo_server_thread_get_server(st), LO_TT_IMMEDIATE, cmd, "iii", e->x, e->y, e->event_type);
	free(cmd);
}

void add_osc_methods(char *prefix, monome_t *monome) {
	char *cmd_buf;
	
	asprintf(&cmd_buf, "/%s/clear", prefix);
	lo_server_thread_add_method(st, cmd_buf, "", clear_handler, monome);
	lo_server_thread_add_method(st, cmd_buf, "i", clear_handler, monome);
	free(cmd_buf);
	
	asprintf(&cmd_buf, "/%s/led", prefix);
	lo_server_thread_add_method(st, cmd_buf, "iii", led_handler, monome);
	free(cmd_buf);
	
	asprintf(&cmd_buf, "/%s/led_row", prefix);
	lo_server_thread_add_method(st, cmd_buf, "ii", led_col_row_handler, monome);
	lo_server_thread_add_method(st, cmd_buf, "iii", led_col_row_handler, monome);
	free(cmd_buf);
	
	asprintf(&cmd_buf, "/%s/led_col", prefix);
	lo_server_thread_add_method(st, cmd_buf, "ii", led_col_row_handler, monome);
	lo_server_thread_add_method(st, cmd_buf, "iii", led_col_row_handler, monome);
	free(cmd_buf);
	
	asprintf(&cmd_buf, "/%s/frame", prefix);
	lo_server_thread_add_method(st, cmd_buf, "iiiiiiii", frame_handler, monome);
	lo_server_thread_add_method(st, cmd_buf, "iiiiiiiii", frame_handler, monome);
	lo_server_thread_add_method(st, cmd_buf, "iiiiiiiiii", frame_handler, monome);
	free(cmd_buf);
}

void del_osc_methods(char *prefix) {
	char *cmd_buf;
	
	asprintf(&cmd_buf, "/%s/clear", prefix);
	lo_server_thread_del_method(st, cmd_buf, "");
	lo_server_thread_del_method(st, cmd_buf, "i");
	free(cmd_buf);
	
	asprintf(&cmd_buf, "/%s/led", prefix);
	lo_server_thread_del_method(st, cmd_buf, "iii");
	free(cmd_buf);
	
	asprintf(&cmd_buf, "/%s/led_row", prefix);
	lo_server_thread_del_method(st, cmd_buf, "ii");
	lo_server_thread_del_method(st, cmd_buf, "iii");
	free(cmd_buf);

	asprintf(&cmd_buf, "/%s/led_col", prefix);
	lo_server_thread_del_method(st, cmd_buf, "ii");
	lo_server_thread_del_method(st, cmd_buf, "iii");
	free(cmd_buf);

	asprintf(&cmd_buf, "/%s/frame", prefix);
	lo_server_thread_del_method(st, cmd_buf, "iiiiiiii");
	lo_server_thread_del_method(st, cmd_buf, "iiiiiiiii");
	lo_server_thread_del_method(st, cmd_buf, "iiiiiiiiii");
	free(cmd_buf);
}

void usage(const char *app) {
	printf("usage: %s [options...] [prefix]\n"
		   "\n"
		   "  -h, --help			display this information\n"
		   "\n"
		   "  -d, --device <device>		the monome serial device\n"
		   "  -p, --protocol <protocol>	which protocol to use (\"40h\" or \"series\")\n"
		   "\n"
		   "  -s, --server-port <port>	what port to listen on\n"
		   "  -a, --application-port <port>	what port to talk to\n"
		   "  -o, --application-host <host> the host your application is on\n"
		   "\n", app);
}

int is_numstr(const char *s) {
	while((48 <= *s) && (*s++ <= 57)); /* 48 is ASCII '0', 57 is '9' */

	if( *--s ) /* if the character we stopped on isn't a null, we didn't make it through the string */
		return 0; /* oh well :( */
	return 1;
}

int main(int argc, char *argv[]) {
	monome_t *monome;
	char c, *device, *sport, *aport, *ahost, *proto;
	int i;

	struct option arguments[] = {
		{"help",             no_argument,       0, 'h'},

		{"device",           required_argument, 0, 'd'},
		{"protocol",         required_argument, 0, 'p'},

		{"server-port",      required_argument, 0, 's'},
		{"application-port", required_argument, 0, 'a'},
		{"application-host", required_argument, 0, 'o'}
	};

	device = DEFAULT_MONOME_DEVICE;
	proto  = DEFAULT_MONOME_PROTOCOL;
	sport  = DEFAULT_OSC_SERVER_PORT;
	aport  = DEFAULT_OSC_APP_PORT;
	ahost  = DEFAULT_OSC_APP_HOST;

	while( (c = getopt_long(argc, argv, "hd:p:s:a:o:", arguments, &i)) > 0 ) {
		switch( c ) {
		case 'h':
			usage(argv[0]);
			return 1;
			
		case 'd':
			device = optarg;
			break;

		case 'p':
			proto = optarg;
			break;

		case 's':
			if( is_numstr(optarg) )
				sport = optarg;
			else
				printf("warning: \"%s\" is not a valid server port.\n", optarg);

			break;

		case 'a':
			if( is_numstr(optarg) )
				aport = optarg;
			else
				printf("warning: \"%s\" is not a valid application port.\n", optarg);

			break;

		case 'o':
			ahost = optarg;
			break;
		}
	}

	if( optind == argc ) {
		lo_prefix = calloc(sizeof(char), strlen(DEFAULT_OSC_PREFIX) + 1);
		strcpy(lo_prefix, DEFAULT_OSC_PREFIX);
	} else
		lo_prefix = strdup(argv[optind]);

	if( !(monome = monome_open(proto, device)) )
		return 1;

	if( !(st = lo_server_thread_new(sport, lo_error)) )
		return -1;
	
	outgoing = lo_address_new(ahost, aport);
	
	monome_register_handler(monome, MONOME_BUTTON_DOWN, handle_press, lo_prefix);
	monome_register_handler(monome, MONOME_BUTTON_UP, handle_press, lo_prefix);
	
	add_osc_methods(lo_prefix, monome);
	
	monome_clear(monome, MONOME_CLEAR_OFF);
	
	lo_server_thread_start(st);
	monome_main_loop(monome);
	
	monome_close(monome);
	free(lo_prefix);
	
	return 0;
}
