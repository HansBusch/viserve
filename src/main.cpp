/*
 * Copyright 2022 Hans Busch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/timeb.h>
#include <stdarg.h>
#include "vito_io.h"
#include <chrono>
#include <thread>

#ifdef _WIN32 
#  define timeb _timeb
#  define ftime _ftime
#  define S_ISREG(x) (x & _S_IFREG)
#endif

#include "restapi.h"
#include "pugixml/pugixml.hpp"

static FILE *fdLog;
static int logLevel;
static const char* wwwRoot;

void printTimestamp(FILE *fd, const char *prefix) {
    struct timeb time;
    ftime(&time);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&time.time));
    fprintf(fd, "\n%s.%03d %s ", buffer, time.millitm, prefix);
}
int logText(const char *prefix, const char *fmt, ...) {
    printTimestamp(fdLog, prefix);
    va_list args;
    va_start(args, fmt);
    vfprintf(fdLog, fmt, args);
    va_end(args);
    fflush(fdLog);
    return -1;
}
void logDump(int level, const char *prefix, int addr, const void *data, size_t size) {
    if (logLevel < level) return;
    printTimestamp(fdLog, prefix);
    if (addr > 0) fprintf(fdLog, "%04x ", addr);
    uint8_t *d = (uint8_t*)data;
    while (size-- != 0) fprintf(fdLog, "%02x", *d++);
    fflush(fdLog);
}

#define EMPTY_PAGE "<html><body>File not found</body></html>"
static ssize_t file_reader (void *fd, uint64_t pos, char *buf, size_t max)
{
  FILE *file = (FILE*)fd;

  (void) fseek (file, (long)pos, SEEK_SET);
  return fread (buf, 1, max, file);
}

static void file_free (void *fd)
{
  FILE *file = (FILE*)fd;
  fclose (file);
}

#define GET_MAGIC 0x12341234
#define PUT_MAGIC 0xff001234

static void
request_completed_callback(void* cls,
    struct MHD_Connection* connection,
    void** con_cls,
    enum MHD_RequestTerminationCode toe)
{
    if (*(uint32_t*)*con_cls == PUT_MAGIC) {
        struct MHD_Response* response = MHD_create_response_from_buffer(0, 0, MHD_RESPMEM_MUST_COPY);
        auto ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    }
    *con_cls = 0;
}


static MHD_Result onHttp(void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size, void **ptr)
{
    struct MHD_Response *response;
    MHD_Result ret;
    struct stat buf;
    static int needsResponse;

    bool get = strcmp(method, MHD_HTTP_METHOD_GET) == 0;

    if (*ptr == 0) {        /* do never respond on first call */
        needsResponse = get ? GET_MAGIC : PUT_MAGIC;
        *ptr = &needsResponse;
        return MHD_YES;
    }

    if (!strncmp(url, "/api", 4)) {
        return onRestApi(connection, url, !get, upload_data, upload_data_size);
    }

    if (!get) return MHD_NO;


    FILE *file = 0;

    if (url[0] == '/' && url[1] == 0) url = "/index.html";
    char path[1024];
    snprintf(path, sizeof(path), "%s%s", wwwRoot, url);
    if (strstr(url, "/..") == 0 &&      // forbid wild navigation 
        (0 == stat(path, &buf)) && (S_ISREG(buf.st_mode))) {
        file = fopen(path, "rb");
    }
    if (file == NULL) {
        response = MHD_create_response_from_buffer(strlen(EMPTY_PAGE),
            (void *)EMPTY_PAGE,
            MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
    }
    else
    {
        response = MHD_create_response_from_callback(buf.st_size, 32 * 1024,     /* 32k PAGE_NOT_FOUND size */
            &file_reader, file,
            &file_free);
        if (response == NULL)
        {
            fclose(file);
            return MHD_NO;
        }
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    }
    return ret;
}

int main(int argc, char *const *argv)
{
    struct MHD_Daemon *daemon;
    pugi::xml_document doc;

    if (!doc.load_file("config.xml")) {
        fprintf(stderr, "Error: failed to load configuration\n");
        return -1;
    }

    auto server = doc.first_element_by_path("config/server");
    auto log = server.child("log");
    logLevel = log.child("level").text().as_int();
    auto logfile = log.child_value("path");
    if (logfile) fdLog = fopen(logfile, "a+");
    if (!fdLog) {
        fprintf(stderr, "Error: failed to open logfile\n");
        fdLog = stderr;
    }

    wwwRoot = server.first_element_by_path("html").text().as_string();

    int port = server.first_element_by_path("http/port").text().as_int();
    int defaultRefresh = server.first_element_by_path("default/refresh").text().as_int(10);

    loadRestApi(ApiRoot, doc.first_element_by_path("config/api"), defaultRefresh, vito_read, vito_write);

    daemon = MHD_start_daemon(MHD_USE_DEBUG | MHD_USE_INTERNAL_POLLING_THREAD,
            port, NULL, NULL, &onHttp, NULL, 
            MHD_OPTION_CONNECTION_TIMEOUT, 256, MHD_OPTION_NOTIFY_COMPLETED, &request_completed_callback, NULL, MHD_OPTION_END);

    logText("--", "http daemon listen on %d", daemon ? port : -1);

    auto usbPort = server.first_element_by_path("usb").text().as_string(0);
    if (usbPort) {
        if (vito_open((char*)usbPort)) {
            fprintf(stderr, "Failed to open serial port. Operating in simulation mode.\n");
            logText("--", "Failed to open serial port. Operating in simulation mode.");
        }
        else {
            vito_init();
        }
    }

    while (1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        onRestTimer();
    }

    MHD_stop_daemon(daemon);

    return 0;
}
