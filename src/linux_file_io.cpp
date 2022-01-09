#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "game.h"
#include "types.h"

size_t platform_get_file_size(const int fd)
{
	struct stat st;
	fstat(fd, &st);
	return st.st_size;
}

buffer platform_read_entire_file(const char* const filename)
{
	buffer buffer = {};

	const auto fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "[IO]: Failed to open file: %s\n", strerror(errno));
		return buffer;
	}

	buffer.size = platform_get_file_size(fd);
	buffer.mem = malloc(buffer.size);

	auto bytes_read = read(fd, buffer.mem, buffer.size);
	if (bytes_read < 0 || (size_t)bytes_read != buffer.size) {
		free(buffer.mem);
	}

	close(fd);
	return buffer;
}

bool platform_write_entire_file(const char* const filename, void* const mem, const u32 mem_size)
{
	const auto fd = open(filename, O_WRONLY | O_CREAT, 0666);
	if (fd < 0) {
		fprintf(stderr, "[IO]: Failed to create file: %s\n", strerror(errno));
		return 0;
	}

	return write(fd, mem, mem_size) == mem_size;
}
