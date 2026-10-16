/* df.c - /bin/df [DEVICE...]: each volume's blocks, used and free, by its
 * own superblock and bitmap read off the device - the devices given, or
 * every one of /dev whose superblock is of a volume its own size, so that
 * a side that is half of a both-sides volume is not told as one. */
#include "unistd.h"
#include "../kernel/fs.h"

#define PATHMAX 64

static unsigned char block[512];

/* The device's blocks by its name's last part: 800 a diskette's side
 * (fd0..fd3), 1600 both sides (fdx0, fdx1), 1024 the RAM disk. */
static unsigned device_blocks(const char *device)
{
	const char *name = device;

	for (; *device; device++)
		if (*device == '/')
			name = device + 1;
	if (name[0] != 'f' || name[1] != 'd')
		return 1024;
	return name[2] == 'x' ? 1600 : 800;
}

static int read_block(int fd, unsigned b)
{
	return lseek(fd, (long)b * 512, 0) >= 0 && read(fd, block, 512) == 512 ? 0 : -1;
}

/* The volume on the device told; 0, or -1 when there is none of its size. */
static int tell(const char *device, int quiet)
{
	const struct superblock *sb = (const struct superblock *)block;
	unsigned blocks, bitmap, b, used = 0;
	int fd = open(device, 0);

	if (fd < 0 || read_block(fd, 1) || sb->magic != FS_MAGIC ||
	    sb->blocks != device_blocks(device) || sb->blocks > 8 * 512) {
		if (fd >= 0)
			close(fd);
		if (!quiet) {
			puts_fd(2, "df: ");
			puts_fd(2, device);
			puts_fd(2, ": no volume there\n");
		}
		return -1;
	}
	blocks = sb->blocks;
	bitmap = sb->bitmap;
	if (read_block(fd, bitmap)) {
		close(fd);
		return -1;
	}
	close(fd);
	for (b = 0; b < blocks; b++)
		used += block[b >> 3] >> (b & 7) & 1;
	puts_fd(1, device);
	puts_fd(1, " ");
	putnum(blocks);
	puts_fd(1, " ");
	putnum(used);
	puts_fd(1, " ");
	putnum(blocks - used);
	puts_fd(1, "\n");
	return 0;
}

int main(int argc, char **argv)
{
	struct {
		unsigned ino;
		char name[14];
	} d;
	char path[PATHMAX];
	struct stat st;
	int i, fd, bad = 0;

	puts_fd(1, "device blocks used free\n");
	if (argc > 1) {
		for (i = 1; i < argc; i++)
			bad |= tell(argv[i], 0) < 0;
		return bad;
	}
	if ((fd = open("/dev", 0)) < 0) {
		complain("df", "/dev");
		return 1;
	}
	while (read(fd, &d, sizeof d) == sizeof d) {
		if (!d.ino || d.name[0] == '.')
			continue;
		memcpy(path, "/dev/", 5);
		for (i = 0; i < 14 && d.name[i]; i++)
			path[5 + i] = d.name[i];
		path[5 + i] = 0;
		if (stat(path, &st) == 0 && (st.mode & S_IFMT) == S_IFBLK)
			tell(path, 1);
	}
	close(fd);
	return 0;
}
