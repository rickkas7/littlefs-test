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


typedef size_t fsblkcnt_t;
typedef size_t fsfilcnt_t;

struct statvfs {
    unsigned long  f_bsize;    /* file system block size */
    unsigned long  f_frsize;   /* fragment size */
    fsblkcnt_t     f_blocks;   /* size of fs in f_frsize units */
    fsblkcnt_t     f_bfree;    /* # free blocks */
    fsblkcnt_t     f_bavail;   /* # free blocks for unprivileged users */
    fsfilcnt_t     f_files;    /* # inodes */
    fsfilcnt_t     f_ffree;    /* # free inodes */
    fsfilcnt_t     f_favail;   /* # free inodes for unprivileged users */
    unsigned long  f_fsid;     /* file system ID */
    unsigned long  f_flag;     /* mount flags */
    unsigned long  f_namemax;  /* maximum filename length */
};

int statvfs(const char* path, struct statvfs* s)
{
    (void)path;

    if (!s) {
        return -1;
    }


    size_t inUse = 0;

    int r = lfs_traverse(&fsInstance, [](void* p, lfs_block_t b) -> int {
        size_t* inUse = (size_t*)p;
        ++(*inUse);
        return 0;
    }, &inUse);

    if (r) {
        return r;
    }

    memset(s, 0, sizeof(*s));

    s->f_bsize = s->f_frsize = fsConfig.block_size;
    s->f_blocks = fsConfig.block_count;
    s->f_bfree = s->f_bavail = s->f_blocks - inUse;
    s->f_namemax = LFS_NAME_MAX;

    return 0;
}

void fs_dump_dir(char* path, size_t len)
{
    lfs_dir_t dir = {};
    int r = lfs_dir_open(&fsInstance, &dir, path);
    size_t pathLen = strnlen(path, len);

    if (r) {
        return;
    }

    printf("%s:\r\n", path);

    struct lfs_info info = {};
    while (true) {
        r = lfs_dir_read(&fsInstance, &dir, &info);
        if (r != 1) {
            break;
        }
        printf("%crw-rw-rw- %8lu %s\r\n", info.type == LFS_TYPE_REG ? '-' : 'd', info.size, info.name);
    }

    printf("\r\n", path);

    r = lfs_dir_rewind(&fsInstance, &dir);

    while (true) {
        r = lfs_dir_read(&fsInstance, &dir, &info);
        if (r != 1) {
            break;
        }
        /* Restore path */
        path[pathLen] = '\0';
        if (info.type == LFS_TYPE_DIR && info.name[0] != '.') {
            int plen = snprintf(path + pathLen, len - pathLen, "%s%s", pathLen != 1 ? "/" : "", info.name);
            if (plen >= (int)(len - pathLen)) {
                /* Didn't fit */
                continue;
            }

            fs_dump_dir(path, len);
        }
    }


    lfs_dir_close(&fsInstance, &dir);
}

void fs_dump()
{
    struct statvfs svfs;
    int r = statvfs(nullptr, &svfs);

    if (!r) {
        printf("%-11s %11s %7s %4s %5s %8s %8s %8s  %4s\r\n",
            "Filesystem",
            "Block size",
            "Blocks",
            "Used",
            "Avail",
            "Size",
            "Used",
            "Avail",
            "Use%");
        printf("%-11s %11lu %7lu %4lu %5lu %8lu %8lu %8lu %4lu%%\r\n\r\n",
            "littlefs",
            svfs.f_bsize,
            svfs.f_blocks,
            svfs.f_blocks - svfs.f_bfree,
            svfs.f_bfree,
            svfs.f_bsize * svfs.f_blocks,
            svfs.f_bsize * (svfs.f_blocks - svfs.f_bfree),
            svfs.f_bsize * svfs.f_bfree,
            (unsigned long)(100.0f - (((float)svfs.f_bfree / (float)svfs.f_blocks) * 100)));
    }

    /* Recursively traverse directories */
    char tmpbuf[(LFS_NAME_MAX + 1) * 2] = {};
    tmpbuf[0] = '/';
    fs_dump_dir(tmpbuf, sizeof(tmpbuf));
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

    fs_dump();
    /*
    lfs_dir_t dir;
    ret = lfs_dir_open(&fsInstance, &dir, "/sys");
    printf("open sys %d\n", ret);
    */

    ret = lfs_deorphan(&fsInstance);
    printf("lfs_deorphan %d\n", ret);

    if (ret) {
        return ret;
    }



    return 0;
}