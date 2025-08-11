#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lfs.h"

struct lfs_config fsConfig;
lfs_t fsInstance;
unsigned char *fsData;
size_t fsDataSize;

int myRead(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {

    memcpy(buffer, &fsData[block * fsConfig.block_size] + off, size);  

    return 0;
}

int myProg(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    for(size_t ii = 0; ii < size; ii++) {        
        ((unsigned char *)buffer)[block * fsConfig.block_size + off + ii] &= fsData[block * fsConfig.block_size + off + ii];
    }

    return 0;
}

int myErase(const struct lfs_config *c, lfs_block_t block) {

    for(size_t ii = 0; ii < fsConfig.block_size; ii++) {     
        fsData[block * fsConfig.block_size + ii] = 0xff;   
    }

    return 0;
}

int mySync(const struct lfs_config *c) {
    return 0;
}


lfs_t *lfs_new(void) {
    return (lfs_t *) malloc(sizeof(lfs_t));
}


struct lfs_info *lfs_new_info(void) {
    return (lfs_info *) malloc(sizeof(struct lfs_info));
}

struct lfs_file *lfs_new_file(void) {
    return (lfs_file *) malloc(sizeof(struct lfs_file));
}

struct lfs_dir *lfs_new_dir(void) {
    return (lfs_dir *) malloc(sizeof(struct lfs_dir));
}


extern "C"
int main(int argc, char *argv[]) {
    FILE *fd = fopen("fs_corrupted.bin", "r");

    fseek(fd, 0, SEEK_END);
    fsDataSize = ftell(fd);
    fseek(fd, 0, SEEK_SET);

    fsData = (unsigned char *)malloc(fsDataSize);
    fread(fsData, 1, fsDataSize, fd);
    fclose(fd);

    printf("read data %d bytes\n", (int)fsDataSize);


    memset(&fsConfig, 0, sizeof(struct lfs_config));

    fsConfig.read = myRead;
    fsConfig.prog = myProg;
    fsConfig.erase = myErase;
    fsConfig.sync = mySync;
    // from hal/src/nRF52840/littlefs/filesystem_impl.h
    fsConfig.read_size = 256; 
    fsConfig.prog_size = 256;
    fsConfig.block_size = 4096; // sFLASH_PAGESIZE
    fsConfig.block_count = 512; // 2 MB
    fsConfig.lookahead = 128;


    int ret = lfs_mount(&fsInstance, &fsConfig);
    printf("lfs_mount %d\n", ret);


    ret = lfs_deorphan(&fsInstance);
    printf("lfs_deorphan %d\n", ret);

    if (ret) {
        return ret;
    }



    return 0;
}