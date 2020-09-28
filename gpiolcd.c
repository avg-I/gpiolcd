/*
 * Control LCD module hung off 8-pin GPIO.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/gpio.h>

#define debug(lev, fmt, args...)	if (debuglevel >= lev) fprintf(stderr, fmt "\n" , ## args);

static void	usage(void);
static char	*progname;

#define	DEFAULT_DEVICE	"/dev/gpioc0"

/* Driver functions */
static void	hd44780_prepare(char *devname, char *options);
static void	hd44780_finish(void);
static void	hd44780_command(int cmd);
static void	hd44780_putc(int c);

/*
 * Commands
 * Note that unrecognised command escapes are passed through with
 * the command value set to the ASCII value of the escaped character.
 */
#define CMD_RESET	0
#define CMD_BKSP	1
#define CMD_CLR		2
#define CMD_NL		3
#define CMD_CR		4
#define CMD_HOME	5

#define MAX_DRVOPT	10	/* maximum driver-specific options */

struct lcd_driver
{
	char	*l_code;
	char	*l_name;
	char	*l_options[MAX_DRVOPT];
	void	(* l_prepare)(char *name, char *options);
	void	(* l_finish)(void);
	void	(* l_command)(int cmd);
	void	(* l_putc)(int c);
};

static struct lcd_driver lcd_drivertab[] = {
	{
		"hd44780",
		"Hitachi HD44780 and compatibles",
		{
			"Reset options:",
			"    1     1-line display (default multiple)",
			"    B     Cursor blink enable",
			"    C     Cursor enable",
			"    F     Large font select",
			NULL
		},
		hd44780_prepare,
		hd44780_finish,
		hd44780_command,
		hd44780_putc
	},
	{ 0 }
};

static void	do_char(struct lcd_driver *driver, char ch);

int	debuglevel = 0;
int	vflag = 0;

int
main(int argc, char *argv[])
{
	extern char		*optarg;
	extern int		optind;
	struct lcd_driver	*driver = &lcd_drivertab[0];
	char		*drivertype, *cp;
	char		*devname = DEFAULT_DEVICE;
	char		*drvopts = NULL;
	int			ch, i;

	if ((progname = strrchr(argv[0], '/'))) {
		progname++;
	} else {
		progname = argv[0];
	}

	drivertype = getenv("LCD_TYPE");

	while ((ch = getopt(argc, argv, "Dd:f:o:v")) != -1) {
		switch(ch) {
		case 'D':
			debuglevel++;
			break;
		case 'd':
			drivertype = optarg;
			break;
		case 'f':
			devname = optarg;
			break;
		case 'o':
			drvopts = optarg;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* If an LCD type was specified, look it up */
	if (drivertype != NULL) {
		driver = NULL;
		for (i = 0; lcd_drivertab[i].l_code != NULL; i++) {
			if (!strcmp(drivertype, lcd_drivertab[i].l_code)) {
				driver = &lcd_drivertab[i];
				break;
			}
		}
		if (driver == NULL) {
			warnx("LCD driver '%s' not known", drivertype);
			usage();
		}
	}
	debug(1, "Driver selected for %s", driver->l_name);
	driver->l_prepare(devname, drvopts);
	atexit(driver->l_finish);

	if (argc > 0) {
		debug(2, "reading input from %d argument%s", argc, (argc > 1) ? "s" : "");
		for (i = 0; i < argc; i++)
			for (cp = argv[i]; *cp; cp++)
				do_char(driver, *cp);
	} else {
		debug(2, "reading input from stdin");
		setvbuf(stdin, NULL, _IONBF, 0);
		while ((ch = fgetc(stdin)) != EOF)
			do_char(driver, (char)ch);
	}
	exit(EX_OK);
}

static void
usage(void)
{
	int		i, j;

	fprintf(stderr, "usage: %s [-v] [-d drivername] [-f device] [-o options] [args...]\n", progname);
	fprintf(stderr, "   -D      Increase debugging\n");
	fprintf(stderr, "   -f      Specify device, default is '%s'\n", DEFAULT_DEVICE);
	fprintf(stderr, "   -d      Specify driver, one of:\n");
	for (i = 0; lcd_drivertab[i].l_code != NULL; i++) {
		fprintf(stderr, "              %-10s (%s)%s\n",
		    lcd_drivertab[i].l_code, lcd_drivertab[i].l_name, (i == 0) ? " *default*" : "");
		if (lcd_drivertab[i].l_options[0] != NULL) {

			for (j = 0; lcd_drivertab[i].l_options[j] != NULL; j++)
				fprintf(stderr, "                  %s\n", lcd_drivertab[i].l_options[j]);
		}
	}
	fprintf(stderr, "  -o       Specify driver option string\n");
	fprintf(stderr, "  args     Message strings.  Embedded escapes supported:\n");
	fprintf(stderr, "                  \\b	Backspace\n");
	fprintf(stderr, "                  \\f	Clear display, home cursor\n");
	fprintf(stderr, "                  \\n	Newline\n");
	fprintf(stderr, "                  \\r	Carriage return\n");
	fprintf(stderr, "                  \\R	Reset display\n");
	fprintf(stderr, "                  \\v	Home cursor\n");
	fprintf(stderr, "                  \\\\	Literal \\\n");
	fprintf(stderr, "           If args not supplied, strings are read from standard input\n");
	exit(EX_USAGE);
}

static void
do_char(struct lcd_driver *driver, char ch)
{
	static int	esc = 0;

	if (esc) {
		switch(ch) {
		case 'b':
			driver->l_command(CMD_BKSP);
			break;
		case 'f':
			driver->l_command(CMD_CLR);
			break;
		case 'n':
			driver->l_command(CMD_NL);
			break;
		case 'r':
			driver->l_command(CMD_CR);
			break;
		case 'R':
			driver->l_command(CMD_RESET);
			break;
		case 'v':
			driver->l_command(CMD_HOME);
			break;
		case '\\':
			driver->l_putc('\\');
			break;
		default:
			driver->l_command(ch);
			break;
		}
		esc = 0;
	} else {
		if (ch == '\\') {
			esc = 1;
		} else {
			if (vflag || isprint(ch))
				driver->l_putc(ch);
		}
	}
}


/******************************************************************************
 * Driver for the Hitachi HD44780.  This is probably *the* most common driver
 * to be found on one- and two-line alphanumeric LCDs.
 *
 * This driver assumes the following connections :
 *
 * GPIO         	LCD Module
 * --------------------------------
 * P0            	RS
 * P1            	R/W
 * P2        		E
 * P3        		Backlight control circuit
 * P4-P7      		Data, DB4-DB7
 *
 */

static int	hd_fd;
static int	hd_lines = 4;
static int	hd_blink = 0;
static int 	hd_cursor = 0;
static int	hd_font = 0;

#define HD_COMMAND	0x00
#define HD_DATA		0x01
#define HD_READ		0x02
#define HD_WRITE	0x00
#define HD_EN		0x04

#define HD_BF		0x80		/* internal busy flag */
#define HD_ADDRMASK	0x7f		/* DDRAM address mask */

static void
hd_sctrl(uint8_t val)
{
	struct gpio_access_32 io;
	int err;

	io.first_pin = 0;
	io.clear_pins = 0x07;
	io.change_pins = val & 0x07;
	err = ioctl(hd_fd, GPIOACCESS32, &io);
	if (err != 0)
		debug(1, "%s: error %d\n", __func__, errno);
}

static void
hd_sdata(uint8_t val)
{
	struct gpio_access_32 io;
	int err;

	io.first_pin = 0;
	io.clear_pins = 0xf0;
	io.change_pins = val << 4;
	err = ioctl(hd_fd, GPIOACCESS32, &io);
	if (err != 0)
		debug(1, "%s: error %d\n", __func__, errno);
}

static uint8_t
hd_gdata(void)
{
	struct gpio_access_32 io;
	int err;

	io.first_pin = 0;
	io.clear_pins = 0x00;
	io.change_pins = 0x00;
	err = ioctl(hd_fd, GPIOACCESS32, &io);
	if (err != 0)
		debug(1, "%s: error %d\n", __func__, errno);

	return ((io.orig_pins & 0xf0) >> 4);
}

static void
hd44780_output(int type, int data)
{
	debug(3, "%s -> 0x%02x", (type == HD_COMMAND) ? "cmd " : "data", data);

	hd_sctrl(type | HD_WRITE);		/* set direction, address */

	hd_sdata((uint8_t) data >> 4);		/* drive upper nibble of data */
	usleep(20);
	hd_sctrl(type | HD_WRITE | HD_EN);	/* raise E */
	usleep(40);
	hd_sctrl(type | HD_WRITE);		/* lower E */

	hd_sdata((uint8_t) data & 0x0f);	/* drive lower nibble of data */
	usleep(20);
	hd_sctrl(type | HD_WRITE | HD_EN);	/* raise E */
	usleep(40);
	hd_sctrl(type | HD_WRITE);		/* lower E */
}

static int
hd44780_input(int type)
{
	uint8_t	val;

	hd_sctrl(type | HD_READ);		/* set direction, address */

	hd_sctrl(type | HD_READ | HD_EN);	/* raise E */
	usleep(40);
	hd_sctrl(type | HD_READ);		/* lower E */
	val = hd_gdata();			/* read data */
	val <<= 4;
	usleep(20);


	hd_sctrl(type | HD_READ | HD_EN);	/* raise E */
	usleep(40);
	hd_sctrl(type | HD_READ);		/* lower E */
	val |= hd_gdata();			/* read data */

	debug(3, "0x%02x -> %s", val, (type == HD_COMMAND) ? "cmd " : "data");
	return(val);
}

static void
hd44780_prepare(char *devname, char *options)
{
	struct gpio_pin cfg;
	char *cp = options;
	int error, i;

	if ((hd_fd = open(devname, O_RDWR, 0)) == -1)
		err(EX_OSFILE, "can't open '%s'", devname);

	/* parse options */
	while (cp && *cp) {
		switch (*cp++) {
		case '1':
			hd_lines = 1;
			break;
		case 'B':
			hd_blink = 1;
			break;
		case 'C':
			hd_cursor = 1;
			break;
		case 'F':
			hd_font = 1;
			break;
		default:
			errx(EX_USAGE, "hd44780: unknown option code '%c'", *(cp-1));
		}
	}

	/* Put LCD in idle state */
#if 0
	if (ioctl(hd_fd, PPIGCTRL, &hd_cbits))		/* save other control bits */
		err(EX_IOERR, "ioctl PPIGCTRL failed (not a ppi device?)");
	hd_cbits &= ~(STROBE | SELECTIN | AUTOFEED);	/* set strobe, RS, R/W low */
	debug(2, "static control bits 0x%x", hd_cbits);
	hd_sctrl(STROBE);
	hd_sdata(0);
#endif
	for (i = 0; i < 8; i++) {
		cfg.gp_pin = i;
		cfg.gp_flags = GPIO_PIN_OUTPUT;
		error = ioctl(hd_fd, GPIOSETCONFIG, &cfg);
		if (error != 0)
			err(1, "configuring pin %d as output failed", i);
	}

	hd44780_command(CMD_RESET);
}

static void
hd44780_finish(void)
{
	close(hd_fd);
}

static void
hd44780_command(int cmd)
{
	uint8_t	val;

	switch (cmd) {
	case CMD_RESET:	/* full manual reset and reconfigure as per datasheet */
		debug(1, "hd44780: reset to %d lines, %s font,%s%s cursor",
		    hd_lines, hd_font ? "5x10" : "5x8", hd_cursor ? "" : " no",
		    hd_blink ? " blinking" : "");

		/*
		 * Would need to OR in 0x10 for 8-bit mode,
		 * hardcoded 4-bit mode for now.
		 */
		val = 0x20;				/* set mode bit */

		if (hd_lines != 1)
			val |= 0x08;
		if (hd_font)
			val |= 0x04;
		hd44780_output(HD_COMMAND, val);
		usleep(10000);
		hd44780_output(HD_COMMAND, val);
		usleep(1000);
		hd44780_output(HD_COMMAND, val);
		usleep(1000);

		val = 0x08;				/* dsisplay control, display off */
		hd44780_output(HD_COMMAND, val);
		usleep(1000);
		val |= 0x04;				/* display on */
		if (hd_cursor)
			val |= 0x02;
		if (hd_blink)
			val |= 0x01;
		hd44780_output(HD_COMMAND, val);
		usleep(1000);

		hd44780_output(HD_COMMAND, 0x06);	/* 0x04 - entry mode, shift cursor by increment */
		usleep(1000);
		/* FALLTHROUGH */

	case CMD_CLR:
		hd44780_output(HD_COMMAND, 0x01);	/* clear display command */
		usleep(2000);
		break;

	case CMD_BKSP:
		hd44780_output(HD_COMMAND, 0x10);	/* cursor or display shift command, move cursor left */
		break;

	case CMD_NL:
		/*
		 * 0x80 - set DRAM address.
		 * 0x00 - beginning of the first line,
		 * 0x40 - beginning of the second line.
		 * FIXME: third, forth ?
		 * FIXME: also, need to keep track of the current line.
		 */
		if (hd_lines > 1)
			hd44780_output(HD_COMMAND, 0x80 | 0x40);
		break;

	case CMD_CR:
		/* Reads current address and busy flag. */
		val = hd44780_input(HD_COMMAND) & HD_ADDRMASK;	/* mask character position, save line pos */
		hd44780_output(HD_COMMAND, 0x80 | (val & ~0x0f));	/* FIXME need to know number of columns. */
		break;

	case CMD_HOME:
		hd44780_output(HD_COMMAND, 0x02);	/* just move to address 0, also resets display shift */
		usleep(2000);
		break;

	default:
		if (isprint(cmd)) {
			warnx("unknown command %c", cmd);
		} else {
			warnx("unknown command 0x%x", cmd);
		}
	}
	usleep(40);
}

static void
hd44780_putc(int c)
{
	hd44780_output(HD_DATA, c);
	usleep(40);
}

