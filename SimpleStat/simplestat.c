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

#include "common.h"
#include "count.h"
#include "ioconf.h"
#include "iostat.h"
#include "rd_stats.h"
#include "version.h"

/* GLOBALS */
struct stats_cpu *st_cpu[2];
unsigned long long uptime[2]  = {0, 0};
unsigned long long uptime0[2] = {0, 0};
struct io_stats *st_iodev[2];
struct io_hdr_stats *st_hdr_iodev;
struct io_dlist *st_dev_list;

int iodev_nr = 0;	/* Nb of devices and partitions found. Includes nb of device groups */
int group_nr = 0;	/* Nb of device groups */
int cpu_nr = 0;		/* Nb of processors on the machine */
int dlist_idx = 0;	/* Nb of devices entered on the command line */
int flags = 0;		/* Flag for common options and system state */
unsigned int dm_major;	/* Device-mapper major number */

long interval = 0;
char timestamp[64];

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
			fprintf(stderr, _("Cannot find disk data\n"));
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

	/* Free the structures. */
	io_sys_free();
	sfree_dev_list();

	return 0;
}
