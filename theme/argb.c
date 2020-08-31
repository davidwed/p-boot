#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <endian.h>

int main(int ac, char* av[])
{
	int fds = open(av[1], O_RDONLY);
	int fdd = open(av[2], O_WRONLY | O_CREAT | O_TRUNC, 0666);
	int ret;
	
	if (fds < 0 || fdd < 0)
		return 1;
	
	struct stat st;
	ret = fstat(fds, &st);
	assert(ret == 0);
	
	uint32_t* in = malloc(st.st_size);
	uint32_t* out = malloc(st.st_size);
	assert(in != NULL);
	assert(out != NULL);
	
	ssize_t read_b = read(fds, in, st.st_size);
	assert(read_b == st.st_size);
	
	for (unsigned i = 0; i < st.st_size / 4; i++) {
		uint32_t v = be32toh(in[i]);
		out[i] = (v >> 8) & 0xffffff;
		out[i] |= (v & 0xff) << 24;
	}

	ssize_t wr_b = write(fdd, out, st.st_size);
	assert(wr_b == st.st_size);
	return 0;
}