/* SimpleStat 0.6.0
 *
 * Cameron Stoughton          2015
 *
 * This program is a system resource monitoring tool, primarily based on Iostat and Mpstat from the Sysstat package.
 * 
 * The goal of this program is to demonstrate the ability to provide statistics on any and all processors in the system as well as other devices. It is also possible to log the statistics to a file according to the date and time the log was written.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <mxml.h>

#include "version.h"
#include "iostat.h"
#include "common.h"
#include "ioconf.h"
#include "rd_stats.h"
#include "count.h"

/* GLOBALS */
struct stats_cpu *st_cpu[2];
unsigned long long uptime[2]  = {0, 0};
unsigned long long uptime0[2] = {0, 0};
struct io_stats *st_iodev[2];
struct io_hdr_stats *st_hdr_iodev;
struct io_dlist *st_dev_list;
char group_name[MAX_NAME_LEN];

int iodev_nr = 0;	/* Number of devices and partitions found. Includes nb of device groups */
int group_nr = 0;	/* Number of device groups */
int cpu_nr = 0;		/* Number of processors on the machine */
int dlist_idx = 0;	/* Number of devices entered on the command line */
int flags = 0;		/* Flag for common options and system state */
unsigned int dm_major;	/* Device-mapper major number */

long interval = 0;
char timestamp[64];

double user_data = 0;
double nice_data = 0;
double kernel_data = 0;
double io_data = 0;
double steal_data = 0;
double idle_data = 0;


/*
 * Ask user before writing/creating an XML log of current system data.
 */
void write_log(void)
{
//	mxml_node_t *xml;    /* <?xml ... ?> */
//	mxml_node_t *stats;   /* <stats> */
//	mxml_node_t *cpu;   /* <cpu> */
//	mxml_node_t *user;  /* <user> */
//	mxml_node_t *nice;  /* <nice> */
//	mxml_node_t *kernel;  /* <kernel> */
//	mxml_node_t *io;  /* <io> */
//	mxml_node_t *steal;  /* <steal> */
//	mxml_node_t *idle;  /* <idle> */

/*	char *user_text = "%d", user_data;
	char *nice_text = "%d", nice_data;
	char *kernel_text = "%d", kernel_data;
	char *io_text = "%d", io_data;
	char *steal_text = "%d", steal_data;
	char *idle_text = "%d", idle_data;
*/
	char c;
	printf("Save CPU stats to file? (y or n): ");

    	while (1)
	{
		int valid = 0;
	        scanf("%c", &c);
		c = tolower(c);
		if(c == 'y' || c == 'n')
		{
			valid = 1;
		}
		else
		{
			// Do nothing.
		}
	        if (c == EOF && valid == 1) break; // end of file
       	        if (!isspace(c) && valid == 1)
		{
             		ungetc(c, stdin);
          		break;
        	}
    	}
	if(c == 'y')
	{
		/*xml = mxmlNewXML("1.0");
		stats = mxmlNewElement(xml, "stats");
		cpu = mxmlNewElement(stats, "cpu");
		user = mxmlNewElement(cpu, "user");
        	mxmlNewText(user, 0, user_text);
		nice = mxmlNewElement(cpu, "nice");
        	mxmlNewText(nice, 0, nice_text);
		kernel = mxmlNewElement(cpu, "kernel");
        	mxmlNewText(kernel, 0, kernel_text);
		io = mxmlNewElement(cpu, "io");
        	mxmlNewText(io, 0, io_text);
		steal = mxmlNewElement(cpu, "steal");
        	mxmlNewText(steal, 0, steal_text);
		idle = mxmlNewElement(cpu, "idle");
        	mxmlNewText(idle, 0, idle_text);*/
		printf("You chose Yes (%c).\n", c);
	}
	else
	{
		printf("You chose No (%c).\n", c);
	}
}

/*
 * Save stats for current device.
 */
void save_stats(char *name, int curr, void *st_io, int iodev_nr,
		struct io_hdr_stats *st_hdr_iodev)
{
	int i;
	struct io_hdr_stats *st_hdr_iodev_i;
	struct io_stats *st_iodev_i;

	/* Look for device in data table */
	for (i = 0; i < iodev_nr; i++) {
		st_hdr_iodev_i = st_hdr_iodev + i;
		if (!strcmp(st_hdr_iodev_i->name, name)) {
			break;
		}
	}

	if (i == iodev_nr) {
		/*
		 * This is a new device: Look for an unused entry to store it.
		 * Thus we are able to handle dynamically registered devices.
		 */
		for (i = 0; i < iodev_nr; i++) {
			st_hdr_iodev_i = st_hdr_iodev + i;
			if (!st_hdr_iodev_i->used) {
				/* Unused entry found... */
				st_hdr_iodev_i->used = TRUE; /* Indicate it is now used */
				strcpy(st_hdr_iodev_i->name, name);
				st_iodev_i = st_iodev[!curr] + i;
				memset(st_iodev_i, 0, IO_STATS_SIZE);
				break;
			}
		}
	}
	if (i < iodev_nr) {
		st_hdr_iodev_i = st_hdr_iodev + i;
		if (st_hdr_iodev_i->status == DISK_UNREGISTERED) {
			st_hdr_iodev_i->status = DISK_REGISTERED;
		}
		st_iodev_i = st_iodev[curr] + i;
		*st_iodev_i = *((struct io_stats *) st_io);
	}
	/*
	 * else it was a new device
	 * but there was no free structure to store it.
	 */
}

/*
 * Display CPU stats.
 */
void write_cpu_stat(int curr, unsigned long long itv)
{
	user_data = ll_sp_value(st_cpu[!curr]->cpu_user,   st_cpu[curr]->cpu_user,   itv);
	nice_data = ll_sp_value(st_cpu[!curr]->cpu_nice,   st_cpu[curr]->cpu_nice,   itv);
	kernel_data = ll_sp_value(st_cpu[!curr]->cpu_sys + st_cpu[!curr]->cpu_softirq +
		   st_cpu[!curr]->cpu_hardirq,
		   st_cpu[curr]->cpu_sys + st_cpu[curr]->cpu_softirq +
		   st_cpu[curr]->cpu_hardirq, itv);
	io_data = ll_sp_value(st_cpu[!curr]->cpu_iowait, st_cpu[curr]->cpu_iowait, itv);
	steal_data = ll_sp_value(st_cpu[!curr]->cpu_steal,  st_cpu[curr]->cpu_steal,  itv);
	idle_data = (st_cpu[curr]->cpu_idle < st_cpu[!curr]->cpu_idle) ?
       		0.0 :
       		ll_sp_value(st_cpu[!curr]->cpu_idle,   st_cpu[curr]->cpu_idle,   itv);

	printf("\nCPU Usage");
	printf("\nIn user space:			%6.2f%%", user_data);
	printf("\nIn 'nice' (or, niceness):	%6.2f%%", nice_data);
	printf("\nIn kernel space:		%6.2f%%", kernel_data);
	printf("\nIn outstanding I/O requests:	%6.2f%%", io_data);
	printf("\nIn time stolen from hypervisor:	%6.2f%%", steal_data);
	printf("\nIdle time:			 %6.2f%%", idle_data);
}

/*
 * Show disk stat header.
 */
void write_disk_stat_header(int *fctr)
{
	if (DISPLAY_EXTENDED(flags)) {
		/* Extended stats */
		printf("Device:         rrqm/s   wrqm/s     r/s     w/s");
		if (DISPLAY_MEGABYTES(flags)) {
			printf("    rMB/s    wMB/s");
			*fctr = 2048;
		}
		else if (DISPLAY_KILOBYTES(flags)) {
			printf("    rkB/s    wkB/s");
			*fctr = 2;
		}
		else {
			printf("   rsec/s   wsec/s");
		}
		printf(" avgrq-sz avgqu-sz   await r_await w_await  svctm  %%util\n");
	}
	else {
		/* Basic stats */
		printf("\n\nDevice:            I/O Requests per Second");
		if (DISPLAY_KILOBYTES(flags)) {
			printf("    kB read/s    kB written/s    total kB read    total kB written\n");
			*fctr = 2;
		}
		else if (DISPLAY_MEGABYTES(flags)) {
			printf("    MB read/s    MB written/s    total MB read    total MB written\n");
			*fctr = 2048;
		}
		else {
			printf("   Blk_read/s   Blk_wrtn/s   Blk_read   Blk_wrtn\n");
		}
	}
}

/*
 * Display extended stats, read from partition.
 */
void write_ext_stat(int curr, unsigned long long itv, int fctr,
		    struct io_hdr_stats *shi, struct io_stats *ioi,
		    struct io_stats *ioj)
{
	curr = curr;
	char *devname = NULL;
	struct stats_disk sdc, sdp;
	struct ext_disk_stats xds;
	double r_await, w_await;

	/*
	 * Counters overflows are possible, but don't need to be handled in
	 * a special way: The difference is still properly calculated if the
	 * result is of the same type as the two values.
	 * Exception is field rq_ticks which is incremented by the number of
	 * I/O in progress times the number of milliseconds spent doing I/O.
	 * But the number of I/O in progress (field ios_pgr) happens to be
	 * sometimes negative...
	 */
	sdc.nr_ios    = ioi->rd_ios + ioi->wr_ios;
	sdp.nr_ios    = ioj->rd_ios + ioj->wr_ios;

	sdc.tot_ticks = ioi->tot_ticks;
	sdp.tot_ticks = ioj->tot_ticks;

	sdc.rd_ticks  = ioi->rd_ticks;
	sdp.rd_ticks  = ioj->rd_ticks;
	sdc.wr_ticks  = ioi->wr_ticks;
	sdp.wr_ticks  = ioj->wr_ticks;

	sdc.rd_sect   = ioi->rd_sectors;
	sdp.rd_sect   = ioj->rd_sectors;
	sdc.wr_sect   = ioi->wr_sectors;
	sdp.wr_sect   = ioj->wr_sectors;

	compute_ext_disk_stats(&sdc, &sdp, itv, &xds);

	r_await = (ioi->rd_ios - ioj->rd_ios) ?
		  (ioi->rd_ticks - ioj->rd_ticks) /
		  ((double) (ioi->rd_ios - ioj->rd_ios)) : 0.0;
	w_await = (ioi->wr_ios - ioj->wr_ios) ?
		  (ioi->wr_ticks - ioj->wr_ticks) /
		  ((double) (ioi->wr_ios - ioj->wr_ios)) : 0.0;

	/* Print device name */
	if (DISPLAY_PERSIST_NAME_I(flags)) {
		devname = get_persistent_name_from_pretty(shi->name);
	}
	if (!devname) {
		devname = shi->name;
	}
	if (DISPLAY_HUMAN_READ(flags)) {
		printf("%s\n%13s", devname, "");
	}
	else {
		printf("%-13s", devname);
	}

	/*       rrq/s wrq/s   r/s   w/s  rsec  wsec  rqsz  qusz await r_await w_await svctm %util */
	printf(" %8.2f %8.2f %7.2f %7.2f %8.2f %8.2f %8.2f %8.2f %7.2f %7.2f %7.2f %6.2f %6.2f\n",
	       S_VALUE(ioj->rd_merges, ioi->rd_merges, itv),
	       S_VALUE(ioj->wr_merges, ioi->wr_merges, itv),
	       S_VALUE(ioj->rd_ios, ioi->rd_ios, itv),
	       S_VALUE(ioj->wr_ios, ioi->wr_ios, itv),
	       S_VALUE(ioj->rd_sectors, ioi->rd_sectors, itv) / fctr,
	       S_VALUE(ioj->wr_sectors, ioi->wr_sectors, itv) / fctr,
	       xds.arqsz,
	       S_VALUE(ioj->rq_ticks, ioi->rq_ticks, itv) / 1000.0,
	       xds.await,
	       r_await,
	       w_await,
	       /* The ticks output is biased to output 1000 ticks per second */
	       xds.svctm,
	       /*
	        * Again: Ticks in milliseconds.
		* In the case of a device group (option -g), shi->used is the number of
		* devices in the group. Else shi->used equals 1.
		*/
	       shi->used ? xds.util / 10.0 / (double) shi->used
	                 : xds.util / 10.0);	/* shi->used should never be null here */
}

/*
 * Write basic stats, read from filesystem.
 */
void write_basic_stat(int curr, unsigned long long itv, int fctr,
		      struct io_hdr_stats *shi, struct io_stats *ioi,
		      struct io_stats *ioj)
{
	curr = curr;
	char *devname = NULL;
	unsigned long long rd_sec, wr_sec;

	/* Print device name */
	if (DISPLAY_PERSIST_NAME_I(flags)) {
		devname = get_persistent_name_from_pretty(shi->name);
	}
	if (!devname) {
		devname = shi->name;
	}
	if (DISPLAY_HUMAN_READ(flags)) {
		printf("%s\n%13s", devname, "");
	}
	else {
		printf("%-13s", devname);
	}

	/* Print stats coming from /sys or /proc/diskstats */
	rd_sec = ioi->rd_sectors - ioj->rd_sectors;
	if ((ioi->rd_sectors < ioj->rd_sectors) && (ioj->rd_sectors <= 0xffffffff)) {
		rd_sec &= 0xffffffff;
	}
	wr_sec = ioi->wr_sectors - ioj->wr_sectors;
	if ((ioi->wr_sectors < ioj->wr_sectors) && (ioj->wr_sectors <= 0xffffffff)) {
		wr_sec &= 0xffffffff;
	}

	printf(" 		%8.2f 	%12.2f 	%12.2f 	%10llu 	%10llu\n",
	       S_VALUE(ioj->rd_ios + ioj->wr_ios, ioi->rd_ios + ioi->wr_ios, itv),
	       S_VALUE(ioj->rd_sectors, ioi->rd_sectors, itv) / fctr,
	       S_VALUE(ioj->wr_sectors, ioi->wr_sectors, itv) / fctr,
	       (unsigned long long) rd_sec / fctr,
	       (unsigned long long) wr_sec / fctr);
}

/*
 * Initialize stat structures.
 */
void init_stats(void)
{
	int i;

	/* Allocate structures for CPUs "all" and 0 */
	for (i = 0; i < 2; i++) {
		if ((st_cpu[i] = (struct stats_cpu *) malloc(STATS_CPU_SIZE * 2)) == NULL) {
			perror("malloc");
			exit(4);
		}
		memset(st_cpu[i], 0, STATS_CPU_SIZE * 2);
	}
}

/*
 * Allocate and initialize structures for I/O devices.
 */
void salloc_device(int dev_nr)
{
	int i;

	for (i = 0; i < 2; i++) {
		if ((st_iodev[i] =
		     (struct io_stats *) malloc(IO_STATS_SIZE * dev_nr)) == NULL) {
			perror("malloc");
			exit(4);
		}
		memset(st_iodev[i], 0, IO_STATS_SIZE * dev_nr);
	}

	if ((st_hdr_iodev =
	     (struct io_hdr_stats *) malloc(IO_HDR_STATS_SIZE * dev_nr)) == NULL) {
		perror("malloc");
		exit(4);
	}
	memset(st_hdr_iodev, 0, IO_HDR_STATS_SIZE * dev_nr);
}

/*
 * Look for the specified device in the device list.
 */
int update_dev_list(int *dlist_idx, char *device_name)
{
	int i;
	struct io_dlist *sdli = st_dev_list;

	for (i = 0; i < *dlist_idx; i++, sdli++) {
		if (!strcmp(sdli->dev_name, device_name))
			break;
	}

	if (i == *dlist_idx) {
		/*
		 * Device not found: Store it.
		 * Group names will be distinguished from real device names
		 * thanks to their names which begin with a space.
		 */
		(*dlist_idx)++;
		strncpy(sdli->dev_name, device_name, MAX_NAME_LEN - 1);
	}

	return i;
}

/*
 * Compute device group stats.
 */
void compute_device_groups_stats(int curr)
{
	struct io_stats gdev, *ioi;
	struct io_hdr_stats *shi = st_hdr_iodev;
	int i, nr_disks;

	memset(&gdev, 0, IO_STATS_SIZE);
	nr_disks = 0;

	for (i = 0; i < iodev_nr; i++, shi++) {
		if (shi->used && (shi->status == DISK_REGISTERED)) {
			ioi = st_iodev[curr] + i;

			if (!DISPLAY_UNFILTERED(flags)) {
				if (!ioi->rd_ios && !ioi->wr_ios)
					continue;
			}

			gdev.rd_ios     += ioi->rd_ios;
			gdev.rd_merges  += ioi->rd_merges;
			gdev.rd_sectors += ioi->rd_sectors;
			gdev.rd_ticks   += ioi->rd_ticks;
			gdev.wr_ios     += ioi->wr_ios;
			gdev.wr_merges  += ioi->wr_merges;
			gdev.wr_sectors += ioi->wr_sectors;
			gdev.wr_ticks   += ioi->wr_ticks;
			gdev.ios_pgr    += ioi->ios_pgr;
			gdev.tot_ticks  += ioi->tot_ticks;
			gdev.rq_ticks   += ioi->rq_ticks;
			nr_disks++;
		}
		else if (shi->status == DISK_GROUP) {
			save_stats(shi->name, curr, &gdev, iodev_nr, st_hdr_iodev);
			shi->used = nr_disks;
			nr_disks = 0;
			memset(&gdev, 0, IO_STATS_SIZE);
		}
	}
}

/*
 * Print all stats and uptime.
 */
void write_stats(int curr, struct tm *rectime)
{
	int dev, i, fctr = 1;
	unsigned long long itv;
	struct io_hdr_stats *shi;
	struct io_dlist *st_dev_list_i;

	/* Test stdout */
	TEST_STDOUT(STDOUT_FILENO);

	/* Print time stamp */
	if (DISPLAY_TIMESTAMP(flags)) {
		if (DISPLAY_ISO(flags)) {
			strftime(timestamp, sizeof(timestamp), "%FT%T%z", rectime);
		}
		else {
			strftime(timestamp, sizeof(timestamp), "%x %X", rectime);
		}
		printf("%s\n", timestamp);
#ifdef DEBUG
		if (DISPLAY_DEBUG(flags)) {
			fprintf(stderr, "%s\n", timestamp);
		}
#endif
	}

	/* Interval is multiplied by the number of processors */
	itv = get_interval(uptime[!curr], uptime[curr]);

	if (DISPLAY_CPU(flags)) {
#ifdef DEBUG
		if (DISPLAY_DEBUG(flags)) {
			/* Debug output */
			fprintf(stderr, "itv=%llu st_cpu[curr]{ cpu_user=%llu cpu_nice=%llu "
					"cpu_sys=%llu cpu_idle=%llu cpu_iowait=%llu cpu_steal=%llu "
					"cpu_hardirq=%llu cpu_softirq=%llu cpu_guest=%llu "
					"cpu_guest_nice=%llu }\n",
				itv,
				st_cpu[curr]->cpu_user,
				st_cpu[curr]->cpu_nice,
				st_cpu[curr]->cpu_sys,
				st_cpu[curr]->cpu_idle,
				st_cpu[curr]->cpu_iowait,
				st_cpu[curr]->cpu_steal,
				st_cpu[curr]->cpu_hardirq,
				st_cpu[curr]->cpu_softirq,
				st_cpu[curr]->cpu_guest,
				st_cpu[curr]->cpu_guest_nice);
		}
#endif

		/* Display CPU utilization */
		write_cpu_stat(curr, itv);
	}

	if (cpu_nr > 1) {
		/* On SMP machines, reduce itv to one processor (see note above) */
		itv = get_interval(uptime0[!curr], uptime0[curr]);
	}

	if (DISPLAY_DISK(flags)) {
		struct io_stats *ioi, *ioj;

		shi = st_hdr_iodev;

		/* Display disk stats header */
		write_disk_stat_header(&fctr);

		for (i = 0; i < iodev_nr; i++, shi++) {
			if (shi->used) {

				if (dlist_idx && !HAS_SYSFS(flags)) {
					/*
					 * With /proc/diskstats, stats for every device
					 * are read even if we have entered a list on devices
					 * on the command line. Thus we need to check
					 * if stats for current device are to be displayed.
					 */
					for (dev = 0; dev < dlist_idx; dev++) {
						st_dev_list_i = st_dev_list + dev;
						if (!strcmp(shi->name, st_dev_list_i->dev_name))
							break;
					}
					if (dev == dlist_idx)
						/* Device not found in list: Don't display it */
						continue;
				}

				ioi = st_iodev[curr] + i;
				ioj = st_iodev[!curr] + i;

				if (!DISPLAY_UNFILTERED(flags)) {
					if (!ioi->rd_ios && !ioi->wr_ios)
						continue;
				}

				if (DISPLAY_ZERO_OMIT(flags)) {
					if ((ioi->rd_ios == ioj->rd_ios) &&
						(ioi->wr_ios == ioj->wr_ios))
						/* No activity: Ignore it */
						continue;
				}

				if (DISPLAY_GROUP_TOTAL_ONLY(flags)) {
					if (shi->status != DISK_GROUP)
						continue;
				}
#ifdef DEBUG
				if (DISPLAY_DEBUG(flags)) {
					/* Debug output */
					fprintf(stderr, "name=%s itv=%llu fctr=%d ioi{ rd_sectors=%lu "
							"wr_sectors=%lu rd_ios=%lu rd_merges=%lu rd_ticks=%u "
							"wr_ios=%lu wr_merges=%lu wr_ticks=%u ios_pgr=%u tot_ticks=%u "
							"rq_ticks=%u }\n",
						shi->name,
						itv,
						fctr,
						ioi->rd_sectors,
						ioi->wr_sectors,
						ioi->rd_ios,
						ioi->rd_merges,
						ioi->rd_ticks,
						ioi->wr_ios,
						ioi->wr_merges,
						ioi->wr_ticks,
						ioi->ios_pgr,
						ioi->tot_ticks,
						ioi->rq_ticks
						);
				}
#endif

				if (DISPLAY_EXTENDED(flags)) {
					write_ext_stat(curr, itv, fctr, shi, ioi, ioj);
				}
				else {
					write_basic_stat(curr, itv, fctr, shi, ioi, ioj);
				}
			}
		}
		printf("\n");
	}
}

/*
 * Allocate structures for all CPU and I/O devices.
 */
void salloc_dev_list(int list_len)
{
	if ((st_dev_list = (struct io_dlist *) malloc(IO_DLIST_SIZE * list_len)) == NULL) {
		perror("malloc");
		exit(4);
	}
	memset(st_dev_list, 0, IO_DLIST_SIZE * list_len);
}

/*
 * Set the output unit for disks.
 */
void set_disk_output_unit(void)
{
	char *e;

	if (DISPLAY_KILOBYTES(flags) || DISPLAY_MEGABYTES(flags))
		return;

	/* Check POSIXLY_CORRECT environment variable */
	if ((e = getenv(ENV_POSIXLY_CORRECT)) == NULL) {
		/* Variable not set: Unit is kB/s and not blocks/s */
		flags |= I_D_KILOBYTES;
	}
}

/*
 * Allocate and initialize structures.
 */
void io_sys_init(void)
{
	/* Allocate and init stat common counters */
	init_stats();

	/* How many processors on this machine? */
	cpu_nr = get_cpu_nr(~0, FALSE);

	/* Get number of block devices and partitions in /proc/diskstats. */
	if ((iodev_nr = get_diskstats_dev_nr(CNT_PART, CNT_ALL_DEV)) > 0)
        {
		flags |= I_F_HAS_DISKSTATS;
		iodev_nr += NR_DEV_PREALLOC;
	}

	if (!HAS_DISKSTATS(flags) ||
	    (DISPLAY_PARTITIONS(flags) && !DISPLAY_PART_ALL(flags)))
        {
		/*
		 * If /proc/diskstats exists but we also want stats for the partitions
		 * of a particular device, stats will have to be found in /sys.
		 * So we need to know if /sys is mounted or not, and set flags accordingly.
		 */

		/* Get number of block devices (and partitions) in sysfs. */
		if ((iodev_nr = get_sysfs_dev_nr(DISPLAY_PARTITIONS(flags))) > 0) {
			flags |= I_F_HAS_SYSFS;
			iodev_nr += NR_DEV_PREALLOC;
		}
		else {
			fprintf(stderr, "Cannot find disk data\n");
			exit(2);
		}
	}

	/* Also allocate stat structures for "group" devices */
	iodev_nr += group_nr;

	/*
	 * Allocate structures for number of disks found, but also
	 * for groups of devices if option -g has been entered on the command line.
	 * iodev_nr must be <> 0.
	 */
	salloc_device(iodev_nr);
}

/*
 * Save the devices and group names when the stats are about to be displayed.
 */
void presave_device_list(void)
{
	int i;
	struct io_hdr_stats *shi = st_hdr_iodev;
	struct io_dlist *sdli = st_dev_list;

	if (dlist_idx>0)
        {
		/* First, save the last group name entered on the command line in the list */
		update_dev_list(&dlist_idx, group_name);

		/* Now save devices and group names in the io_hdr_stats structures */
		for (i = 0; (i < dlist_idx) && (i < iodev_nr); i++, shi++, sdli++)
                {
			strcpy(shi->name, sdli->dev_name);
			shi->used = TRUE;
			if (shi->name[0] == ' ')
                        {
				/* Current device name is in fact the name of a group */
				shi->status = DISK_GROUP;
			}
			else
                        {
				shi->status = DISK_REGISTERED;
			}
		}
	}
	else
        {
		/*
		 * No device names have been entered on the command line but
		 * the name of a group. Save that name at the end of the
		 * data table so that all devices that will be read will be
		 * included in that group.
		 */
		shi += iodev_nr - 1;
		strcpy(shi->name, group_name);
		shi->used = TRUE;
		shi->status = DISK_GROUP;
	}
}

/*
 * Read stats for current block device.
 */
int read_sysfs_file_stat(int curr, char *filename, char *dev_name)
{
	FILE *fp;
	struct io_stats sdev;
	int i;
	unsigned int ios_pgr, tot_ticks, rq_ticks, wr_ticks;
	unsigned long rd_ios, rd_merges_or_rd_sec, wr_ios, wr_merges;
	unsigned long rd_sec_or_wr_ios, wr_sec, rd_ticks_or_wr_sec;

	/* Try to read given stat file */
	if ((fp = fopen(filename, "r")) == NULL)
		return 0;

	i = fscanf(fp, "%lu %lu %lu %lu %lu %lu %lu %u %u %u %u",
		   &rd_ios, &rd_merges_or_rd_sec, &rd_sec_or_wr_ios, &rd_ticks_or_wr_sec,
		   &wr_ios, &wr_merges, &wr_sec, &wr_ticks, &ios_pgr, &tot_ticks, &rq_ticks);

	if (i == 11) {
		/* Device or partition */
		sdev.rd_ios     = rd_ios;
		sdev.rd_merges  = rd_merges_or_rd_sec;
		sdev.rd_sectors = rd_sec_or_wr_ios;
		sdev.rd_ticks   = (unsigned int) rd_ticks_or_wr_sec;
		sdev.wr_ios     = wr_ios;
		sdev.wr_merges  = wr_merges;
		sdev.wr_sectors = wr_sec;
		sdev.wr_ticks   = wr_ticks;
		sdev.ios_pgr    = ios_pgr;
		sdev.tot_ticks  = tot_ticks;
		sdev.rq_ticks   = rq_ticks;
	}
	else if (i == 4) {
		/* Partition without extended statistics */
		sdev.rd_ios     = rd_ios;
		sdev.rd_sectors = rd_merges_or_rd_sec;
		sdev.wr_ios     = rd_sec_or_wr_ios;
		sdev.wr_sectors = rd_ticks_or_wr_sec;
	}

	if ((i == 11) || !DISPLAY_EXTENDED(flags)) {
		/*
		 * In fact, we _don't_ save stats if it's a partition without
		 * extended stats and yet we want to display ext stats.
		 */
		save_stats(dev_name, curr, &sdev, iodev_nr, st_hdr_iodev);
	}

	fclose(fp);

	return 1;
}

/*
 * Read stats for all partitions of a given device.
 */
void read_sysfs_dlist_part_stat(int curr, char *dev_name)
{
	DIR *dir;
	struct dirent *drd;
	char dfile[MAX_PF_NAME], filename[MAX_PF_NAME];

	snprintf(dfile, MAX_PF_NAME, "%s/%s", SYSFS_BLOCK, dev_name);
	dfile[MAX_PF_NAME - 1] = '\0';

	/* Open current device directory in /sys/block */
	if ((dir = opendir(dfile)) == NULL)
		return;

	/* Get current entry */
	while ((drd = readdir(dir)) != NULL) {
		if (!strcmp(drd->d_name, ".") || !strcmp(drd->d_name, ".."))
			continue;
		snprintf(filename, MAX_PF_NAME, "%s/%s/%s", dfile, drd->d_name, S_STAT);
		filename[MAX_PF_NAME - 1] = '\0';

		/* Read current partition stats */
		read_sysfs_file_stat(curr, filename, drd->d_name);
	}

	/* Close device directory */
	closedir(dir);
}

/*
 * Free any unused (unregistered) entries.
 */
void free_unregistered_entries(int iodev_nr, struct io_hdr_stats *st_hdr_iodev)
{
	int i;
	struct io_hdr_stats *shi = st_hdr_iodev;

	for (i = 0; i < iodev_nr; i++, shi++) {
		if (shi->status == DISK_UNREGISTERED) {
			shi->used = FALSE;
		}
	}
}

/*
 * Set all individual device entries to "unregistered."
 */
void set_entries_unregistered(int iodev_nr, struct io_hdr_stats *st_hdr_iodev)
{
	int i;
	struct io_hdr_stats *shi = st_hdr_iodev;

	for (i = 0; i < iodev_nr; i++, shi++) {
		if (shi->status == DISK_REGISTERED) {
			shi->status = DISK_UNREGISTERED;
		}
	}
}

/*
 * Read stats from directory "/proc/diskstats."
 */
void read_diskstats_stat(int curr)
{
	FILE *fp;
	char line[256], dev_name[MAX_NAME_LEN];
	char *dm_name;
	struct io_stats sdev;
	int i;
	unsigned int ios_pgr, tot_ticks, rq_ticks, wr_ticks;
	unsigned long rd_ios, rd_merges_or_rd_sec, rd_ticks_or_wr_sec, wr_ios;
	unsigned long wr_merges, rd_sec_or_wr_ios, wr_sec;
	char *ioc_dname;
	unsigned int major, minor;

	/* Every I/O device entry is potentially unregistered */
	set_entries_unregistered(iodev_nr, st_hdr_iodev);

	if ((fp = fopen(DISKSTATS, "r")) == NULL)
		return;

	while (fgets(line, sizeof(line), fp) != NULL) {

		/* major minor name rio rmerge rsect ruse wio wmerge wsect wuse running use aveq */
		i = sscanf(line, "%u %u %s %lu %lu %lu %lu %lu %lu %lu %u %u %u %u",
			   &major, &minor, dev_name,
			   &rd_ios, &rd_merges_or_rd_sec, &rd_sec_or_wr_ios, &rd_ticks_or_wr_sec,
			   &wr_ios, &wr_merges, &wr_sec, &wr_ticks, &ios_pgr, &tot_ticks, &rq_ticks);

		if (i == 14) {
			/* Device or partition */
			if (!dlist_idx && !DISPLAY_PARTITIONS(flags) &&
			    !is_device(dev_name, ACCEPT_VIRTUAL_DEVICES))
				continue;
			sdev.rd_ios     = rd_ios;
			sdev.rd_merges  = rd_merges_or_rd_sec;
			sdev.rd_sectors = rd_sec_or_wr_ios;
			sdev.rd_ticks   = (unsigned int) rd_ticks_or_wr_sec;
			sdev.wr_ios     = wr_ios;
			sdev.wr_merges  = wr_merges;
			sdev.wr_sectors = wr_sec;
			sdev.wr_ticks   = wr_ticks;
			sdev.ios_pgr    = ios_pgr;
			sdev.tot_ticks  = tot_ticks;
			sdev.rq_ticks   = rq_ticks;
		}
		else if (i == 7) {
			/* Partition without extended statistics */
			if (DISPLAY_EXTENDED(flags) ||
			    (!dlist_idx && !DISPLAY_PARTITIONS(flags)))
				continue;

			sdev.rd_ios     = rd_ios;
			sdev.rd_sectors = rd_merges_or_rd_sec;
			sdev.wr_ios     = rd_sec_or_wr_ios;
			sdev.wr_sectors = rd_ticks_or_wr_sec;
		}
		else
			/* Unknown entry: Ignore it */
			continue;

		if ((ioc_dname = ioc_name(major, minor)) != NULL) {
			if (strcmp(dev_name, ioc_dname) && strcmp(ioc_dname, K_NODEV)) {
				/*
				 * No match: Use name generated from sysstat.ioconf data
				 * (if different from "nodev") works around known issues
				 * with EMC PowerPath.
				 */
				strncpy(dev_name, ioc_dname, MAX_NAME_LEN);
			}
		}

		if ((DISPLAY_DEVMAP_NAME(flags)) && (major == dm_major)) {
			/*
			 * If the device is a device mapper device, try to get its
			 * assigned name of its logical device.
			 */
			dm_name = transform_devmapname(major, minor);
			if (dm_name) {
				strncpy(dev_name, dm_name, MAX_NAME_LEN);
			}
		}

		save_stats(dev_name, curr, &sdev, iodev_nr, st_hdr_iodev);
	}
	fclose(fp);

	/* Free structures corresponding to unregistered devices */
	free_unregistered_entries(iodev_nr, st_hdr_iodev);
}

/*
 * Read stats for devices requested in command line parameters.
 */
void read_sysfs_dlist_stat(int curr)
{
	int dev, ok;
	char filename[MAX_PF_NAME];
	char *slash;
	struct io_dlist *st_dev_list_i;

	/* Every I/O device (or partition) is potentially unregistered */
	set_entries_unregistered(iodev_nr, st_hdr_iodev);

	for (dev = 0; dev < dlist_idx; dev++) {
		st_dev_list_i = st_dev_list + dev;

		/* Some devices may have a slash in their name (eg. cciss/c0d0...) */
		while ((slash = strchr(st_dev_list_i->dev_name, '/'))) {
			*slash = '!';
		}

		snprintf(filename, MAX_PF_NAME, "%s/%s/%s",
			 SYSFS_BLOCK, st_dev_list_i->dev_name, S_STAT);
		filename[MAX_PF_NAME - 1] = '\0';

		/* Read device stats */
		ok = read_sysfs_file_stat(curr, filename, st_dev_list_i->dev_name);

		if (ok && st_dev_list_i->disp_part) {
			/* Also read stats for its partitions */
			read_sysfs_dlist_part_stat(curr, st_dev_list_i->dev_name);
		}
	}

	/* Free structures corresponding to unregistered devices */
	free_unregistered_entries(iodev_nr, st_hdr_iodev);
}

/*
 * Read stats in the filesystem for the devices we have discovered.
 */
void read_sysfs_stat(int curr)
{
	DIR *dir;
	struct dirent *drd;
	char filename[MAX_PF_NAME];
	int ok;

	/* Every I/O device entry is potentially unregistered */
	set_entries_unregistered(iodev_nr, st_hdr_iodev);

	/* Open /sys/block directory */
	if ((dir = opendir(SYSFS_BLOCK)) != NULL) {

		/* Get current entry */
		while ((drd = readdir(dir)) != NULL) {
			if (!strcmp(drd->d_name, ".") || !strcmp(drd->d_name, ".."))
				continue;
			snprintf(filename, MAX_PF_NAME, "%s/%s/%s",
				 SYSFS_BLOCK, drd->d_name, S_STAT);
			filename[MAX_PF_NAME - 1] = '\0';

			/* If current entry is a directory, try to read its stat file */
			ok = read_sysfs_file_stat(curr, filename, drd->d_name);

			/*
			 * If '-p ALL' was entered on the command line,
			 * also try to read stats for its partitions
			 */
			if (ok && DISPLAY_PART_ALL(flags)) {
				read_sysfs_dlist_part_stat(curr, drd->d_name);
			}
		}

		/* Close /sys/block directory */
		closedir(dir);
	}

	/* Free structures corresponding to unregistered devices */
	free_unregistered_entries(iodev_nr, st_hdr_iodev);
}

/*
 * THIS IS THE MOST IMPORTANT LOOP.
 * Read and display I/O stats.
 */
void rw_io_stat_loop(long int count, struct tm *rectime)
{
	int curr = 1;
	int skip = 0;

	/* Should we skip first report? */
	if (DISPLAY_OMIT_SINCE_BOOT(flags) && interval > 0)
        {
		skip = 1;
	}

	/* Don't buffer data if redirected to a pipe. */
	setbuf(stdout, NULL);

	do
        {
		if (cpu_nr > 1)
                {
			/*
			 * Read system uptime (only for SMP machines).
			 * Init uptime0. So if /proc/uptime cannot fill it,
			 * this will be done by /proc/stat.
			 */
			uptime0[curr] = 0;
			read_uptime(&(uptime0[curr]));
		}

		/*
		 * Read stats for CPU "all" and 0.
		 * Note that stats for CPU 0 are not used per se. It only makes
		 * read_stat_cpu() fill uptime0.
		 */
		read_stat_cpu(st_cpu[curr], 2, &(uptime[curr]), &(uptime0[curr]));

		if (dlist_idx)
                {
			/*
			 * A device or partition name was explicitly entered
			 * on the command line, with or without -p option
			 * (but not -p ALL).
			 */
			if (HAS_DISKSTATS(flags) && !DISPLAY_PARTITIONS(flags))
                        {
				read_diskstats_stat(curr);
			}
			else if (HAS_SYSFS(flags)) {
				read_sysfs_dlist_stat(curr);
			}
		}
		else
                {
			/*
			 * No devices nor partitions entered on the command line
			 * (for example if -p ALL was used).
			 */
			if (HAS_DISKSTATS(flags))
                        {
				read_diskstats_stat(curr);
			}
			else if (HAS_SYSFS(flags))
                        {
				read_sysfs_stat(curr);
			}
		}

		/* Compute device groups stats */
		if (group_nr > 0)
                {
			compute_device_groups_stats(curr);
		}

		/* Get time */
		get_localtime(rectime, 0);

		/* Check whether we should skip first report */
		if (!skip)
                {
			/* Print results */
			write_stats(curr, rectime);

			if (count > 0)
                        {
				count--;
			}
		}
		else
                {
			skip = 0;
		}

		if (count)
                {
			curr ^= 1;
			pause();
		}
	}
	while (count);
}

/*
 * Free structures.
 */
void io_sys_free(void)
{
	int i;

	for (i = 0; i < 2; i++)
        {
		/* Free CPU structures. */
		free(st_cpu[i]);

		/* Free I/O device structures. */
		free(st_iodev[i]);
	}

	free(st_hdr_iodev);
}

/*
 * Free the structures we used for the system's devices.
 */
void sfree_dev_list(void)
{
	free(st_dev_list);
}


/*
 * MAIN PROGAM
 */
int main(int argc, char **argv)
{
        int report_set = FALSE;
        long count = 1;
	struct tm rectime;

	/* Allocate structures for the device list. */
	if (argc > 1)
        {
		salloc_dev_list(argc - 1 + count_csvalues(argc, argv));
	}

        /* There are no options for this program, so just run normally. */
        //usage(argv[0]);

        /* Provide all CPU and DISK stats. */
	if (!report_set)
        {
		flags |= I_D_CPU + I_D_DISK;
	}

	/* Select disk output unit (kB/s or blocks/s). */
	set_disk_output_unit();

        /* Initialize structures from the machine architecture. */
	io_sys_init();

        /* If we have defined our device group, save the devices and device groups into the device list. */
	if (group_nr > 0)
        {
		presave_device_list();
	}

        /* Make a timestamp for the moment this program runs. */
	get_localtime(&rectime, 0);

	/* This is the main loop from which we may obtain our I/O stats. */
	rw_io_stat_loop(count, &rectime);

	/* Ask user whether or not to write an XML log. */
	write_log();

	/* Free the structures. */
	io_sys_free();
	sfree_dev_list();

	return 0;
}
