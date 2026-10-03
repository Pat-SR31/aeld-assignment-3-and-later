#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
    openlog("writer", 0, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: %d", argc);
        return 1;
    }

    char *writefile = argv[1];
    char *writestr = argv[2];

    FILE *fd = fopen(writefile, "w");
    if (fd == NULL) {
        syslog(LOG_ERR, "Could not open file %s", writefile);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    fprintf(fd, "%s", writestr);
    fclose(fd);

    return 0;
}
