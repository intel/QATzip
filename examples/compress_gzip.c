#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <qatzip.h>

static int compressFile(int in_file, int out_file)
{
    long input_file_size = 0;
    long output_file_size = 0;
    unsigned int src_buffer_size = 0;
    unsigned int dst_buffer_size = 0;
    unsigned char *src_buffer = NULL;
    unsigned char *dst_buffer = NULL;
    unsigned int bytes_read = 0;
    int rc;
    int is_compress = 1;
    QzSession_T session = {0};
    QzSessionParamsDeflate_T params;

    rc = qzGetDefaultsDeflate(&params);
    if (rc != QZ_OK) {
        fprintf(stderr, "qzGetDefaultsDeflate failed\n");
        return -1;
    }

    rc = qzInit(&session, 0);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        fprintf(stderr, "qzInit failed\n");
        return -1;
    }

    // You can customize parameters(ie, level, pollingmode)
    // before setup
    params.common_params.polling_mode = QZ_BUSY_POLLING;
    params.common_params.comp_lvl = 9;
    params.common_params.hw_buff_sz = 16 * 1024;    //must be a power of 2k(4k 8k 16k 64k 128k)
    rc = qzSetupSessionDeflate(&session, &params);
    if (rc != QZ_OK && rc != QZ_DUPLICATE) {
        fprintf(stderr, "qzSetupSessionDeflate failed\n");
        return -1;
    }

    input_file_size = lseek(in_file, 0, SEEK_END);
    lseek(in_file, 0, SEEK_SET);

    src_buffer_size = input_file_size;

    dst_buffer_size = qzMaxCompressedLength(src_buffer_size, &session);

    src_buffer = malloc(src_buffer_size);
    assert(src_buffer != NULL);
    dst_buffer = malloc(dst_buffer_size);
    assert(dst_buffer != NULL);

    bytes_read = read(in_file, src_buffer, src_buffer_size);
    printf("Reading input file (%u Bytes)\n", bytes_read);
    assert(bytes_read == input_file_size);
    puts("compressing...");

    int ret = qzCompress(&session, src_buffer, (unsigned int *)(&bytes_read), dst_buffer, &dst_buffer_size, 1);
    assert(QZ_OK == ret);

    size_t write_size = write(out_file, dst_buffer, dst_buffer_size);
    assert(write_size == dst_buffer_size);

    free(src_buffer);
    free(dst_buffer);
    qzTeardownSession(&session);
    qzClose(&session);

    return 0;
}

static char* createOutFilename(const char* filename)
{
    size_t const inL = strlen(filename);
    size_t const outL = inL + 4;
    void* const outSpace = malloc(outL);
    memset(outSpace, 0, outL);
    strcat(outSpace, filename);
    strcat(outSpace, ".gz");
    return (char*)outSpace;
}

int main(int argc, const char** argv)
{
    const char* const exeName = argv[0];
    int in_file = -1;
    int out_file = -1;
    struct stat in_file_state;

    if (argc!=2) {
        printf("wrong arguments\n");
        printf("usage:\n");
        printf("%s FILE\n", exeName);
        return 1;
    }

    const char* const inFilename = argv[1];

    char* const outFilename = createOutFilename(inFilename);

    in_file = open(inFilename, O_RDONLY);
    if (in_file < 0) {
        perror("Cannot open input file");
        return -1;
    }

        if (fstat(in_file, &in_file_state)) {
        perror("Cannot get file stat");
        close(in_file);
        return -1;
    }

    out_file = open(outFilename, O_CREAT | O_WRONLY,
                    in_file_state.st_mode);
    if (out_file == -1) {
        perror("Cannot open output file");
        close(in_file);
        return -1;
    }

    compressFile(in_file, out_file);
    close(in_file);
    close(out_file);
    free(outFilename);
    return 0;
}