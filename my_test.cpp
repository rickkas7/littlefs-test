#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#include "lfs.h"

// Typical usage: make my_test && ./my_test fs_corrupted.bin

struct lfs_config fsConfig;
lfs_t fsInstance;
unsigned char *fsData;
size_t fsDataSize;
char localPath[(LFS_NAME_MAX + 1) * 2 + 20];
size_t localPathLen = 0;
const char *argFilename = 0;
bool argModeAnalyze = false;
bool argModeBuild = false;
bool argExtractFilesystem = false;

int myRead(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {

    memcpy(buffer, &fsData[block * fsConfig.block_size] + off, size);  

    return 0;
}

int myProg(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    for(size_t ii = 0; ii < size; ii++) {        
        fsData[block * fsConfig.block_size + off + ii] &= ((const unsigned char *)buffer)[ii];
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
        printf("lfs_traverse failed %d\n", r);
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
        printf("lfs_dir_open failed %d %s\n", r, path);
        return;
    }

    printf("%s:\n", path);

    struct lfs_info info = {};
    while (true) {
        r = lfs_dir_read(&fsInstance, &dir, &info);
        if (r != 1) {
            break;
        }
        printf("%crw-rw-rw- %8lu %s\n", info.type == LFS_TYPE_REG ? '-' : 'd', info.size, info.name);

        if (info.type == LFS_TYPE_REG) {
            size_t savedOutputDirLen = strlen(localPath);

            strcat(localPath, "/");
            strcat(localPath, info.name);

            char sourcePath[(LFS_NAME_MAX + 1) * 2 + 20];
            strcpy(sourcePath, path);
            strcat(sourcePath, "/");
            strcat(sourcePath, info.name);
    
            lfs_file_t file;

            int ret = lfs_file_open(&fsInstance, &file, sourcePath, LFS_O_RDONLY);
            if (ret >= 0) {
                lfs_soff_t size = lfs_file_seek(&fsInstance, &file, 0, LFS_SEEK_END);

                lfs_file_seek(&fsInstance, &file, 0, LFS_SEEK_SET);

                if (size) {
                    char *buf = (char *)malloc(size);
                    if (buf) {
                        ret = lfs_file_read(&fsInstance, &file, buf, size);
                        if (ret == size) {
                            // printf("successfully read file\n");

                            if (argExtractFilesystem) {
                                FILE *fd = fopen(localPath, "w+");
                                if (fd) {
                                    fwrite(buf, 1, size, fd);
                                    fclose(fd);
                                }
                            }
                        }
                        else {
                            printf("file error %d %s\n", ret, sourcePath);
                        }
                    }
                    free(buf);
                }
                else {
                    // printf("file size was 0 %s\n", sourcePath);
                    if (argExtractFilesystem) {
                        FILE *fd = fopen(localPath, "w+");
                        if (fd) {
                            fclose(fd);
                        }
                    }
                }

                lfs_file_close(&fsInstance, &file);
            }
            else {
                printf("failed to open file %d %s\n", ret, sourcePath);
            }
        
            localPath[savedOutputDirLen] = 0;
        }

    }

    printf("\n");

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

            localPath[localPathLen] = 0;
            strcat(localPath, "/");
            strcat(localPath, path);
            if (argExtractFilesystem) {
                mkdir(localPath, 0777);
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
        printf("%-11s %11s %7s %4s %5s %8s %8s %8s  %4s\n",
            "Filesystem",
            "Block size",
            "Blocks",
            "Used",
            "Avail",
            "Size",
            "Used",
            "Avail",
            "Use%");
        printf("%-11s %11lu %7lu %4lu %5lu %8lu %8lu %8lu %4lu%%\n\n",
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

int analyzeFs() {
    printf("--analyze\n");

    FILE *fd = fopen(argFilename, "r");

    fseek(fd, 0, SEEK_END);
    fsDataSize = ftell(fd);
    fseek(fd, 0, SEEK_SET);

    fsData = (unsigned char *)malloc(fsDataSize);
    fread(fsData, 1, fsDataSize, fd);
    fclose(fd);

    printf("read file system data %d bytes\n", (int)fsDataSize);

    fsConfig.block_count = fsDataSize / fsConfig.block_size;

    int ret = lfs_mount(&fsInstance, &fsConfig);
    printf("lfs_mount %d\n", ret);

    fs_dump();

    ret = lfs_deorphan(&fsInstance);
    printf("lfs_deorphan %d\n", ret);

    return ret;
}


int buildFsDir(char *relativePath) {

    char localRelativePath[(LFS_NAME_MAX + 1) * 2];
    strcpy(localRelativePath, localPath);
    if (relativePath[strlen(relativePath) - 1] != '/') {
        strcat(relativePath, "/");
    }
    strcat(localRelativePath, relativePath); 
    size_t savedLocalRelativePathLen = strlen(localRelativePath);

    printf("buildFsDir relativePath=%s localRelativePath=%s\n", relativePath, localRelativePath);

    size_t savedRelativePathLen = strlen(relativePath);
    
    DIR *dirp = opendir(localRelativePath);
    
    while(true) {
        struct dirent *de = readdir(dirp);
        if (!de) {
            break;
        }

        if (relativePath[strlen(relativePath) - 1] != '/') {
            strcat(relativePath, "/");
        }
        strcat(relativePath, de->d_name);

        if (de->d_type == DT_DIR) {
            if (de->d_name[0] != '.') {

                lfs_mkdir(&fsInstance, relativePath);

                buildFsDir(relativePath);

            }
        }
        else
        if (de->d_type == DT_REG) {
            if (localRelativePath[strlen(localRelativePath) - 1] != '/') {
                strcat(localRelativePath, "/");
            }
            strcat(localRelativePath, de->d_name);

            FILE *fd = fopen(localRelativePath, "r");
            if (fd) {
                fseek(fd, 0, SEEK_END);
                size_t fileSize = ftell(fd);
                fseek(fd, 0, SEEK_SET);

                void *buf = 0;
                if (fileSize > 0) {
                    buf = malloc(fileSize);

                    fread(buf, 1, fileSize, fd);
                }

                lfs_file_t file;

                int ret = lfs_file_open(&fsInstance, &file, relativePath, LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC);
                if (ret >= 0) {
                    if (fileSize) {
                        lfs_file_write(&fsInstance, &file, buf, fileSize);
                    }
                    lfs_file_close(&fsInstance, &file);
                }
                else {
                    printf("failed to open %d %d", ret, relativePath);    
                }
                fclose(fd);
            }


            localRelativePath[savedLocalRelativePathLen] = 0;
        }
        
        relativePath[savedRelativePathLen] = 0;
        
    }

    closedir(dirp);
    return 0;
}

int buildFs() {
    int ret = 0;
    
    printf("--build\n");

    // This is fixed at 2 MB, might make configurable to 4MB later
    fsDataSize = 2048 * 1024;

    fsData = (unsigned char *)malloc(fsDataSize);
    memset(fsData, 0xff, fsDataSize);

    fsConfig.block_count = fsDataSize / fsConfig.block_size;

    ret = lfs_format(&fsInstance, &fsConfig);
    printf("lfs_format %d\n", ret);

    if (!ret) {
        ret = lfs_mount(&fsInstance, &fsConfig);
        printf("lfs_mount %d\n", ret);
    }

    char relativePath[(LFS_NAME_MAX + 1) * 2] = {0};
    relativePath[0] = '/';

    buildFsDir(relativePath);

    fs_dump();

    FILE *fd = fopen(argFilename, "w+");
    fwrite(fsData, 1, fsDataSize, fd);
    fclose(fd);


    return ret;
}

extern "C"
int main(int argc, char *argv[]) {
    for(int ii = 1; ii < argc; ii++) {
        if (argv[ii][0] == '-') {
            if (strcmp(argv[ii], "--analyze") == 0) {
                argModeAnalyze = true;
                argModeBuild = false;
            } 
            else
            if (strcmp(argv[ii], "--build") == 0) {
                argModeAnalyze = false;
                argModeBuild = true;
            } 
            else
            if (strcmp(argv[ii], "--extract")== 0) {
                argExtractFilesystem = true;
            }
        }
        else {
            if (argFilename) {
                printf("%s <filename> only one filename allowed\n");
                return 1;
            }
            argFilename = argv[ii];
        }
    }
    if (!argFilename) {
        printf("%s <filename> required\n");
        return 1;
    }
    if (!argModeAnalyze && !argModeBuild) {
        argModeAnalyze = true;
    }

    memset(&fsConfig, 0, sizeof(struct lfs_config));

    fsConfig.read = myRead;
    fsConfig.prog = myProg;
    fsConfig.erase = myErase;
    fsConfig.sync = mySync;
    // from hal/src/nRF52840/littlefs/filesystem_impl.h
    fsConfig.read_size = 256; 
    fsConfig.prog_size = 256;
    fsConfig.block_size = 4096; // sFLASH_PAGESIZE
    fsConfig.block_count = 0; // overridden in analyzeFs or buildFs
    fsConfig.lookahead = 128;

    strcpy(localPath, "./filesystem");
    localPathLen = strlen(localPath);
    struct stat sb;
    if (stat(localPath, &sb)) {
        mkdir(localPath, 0777);
    }



    int ret = 0;

    if (argModeAnalyze) {        
        ret = analyzeFs();
    }
    if (argModeBuild) {
        ret = buildFs();
    }

    return ret;
}