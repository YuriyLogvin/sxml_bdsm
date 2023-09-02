#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
//#include <dirent.h>
#include <C:/Users/banz/esp/esp-idf/components/newlib/platform_include/sys/dirent.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "decrypt/decrypt.c"
#include "decrypt/pc1crypt.c"
#include "decrypt/CrcTool.h"
#include "version.h"
#include "global.h"
#include "BmsInterconnector.h"

#include "lib/sxml/sxml.h"
//#include "lib/tinyxml2/tinyxml2.h"
//using namespace tinyxml2;

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#define SCRATCH_BUFSIZE  8192
#define Decrypt 1

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

struct file_server_data {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
};

static const char *TAG = "file_server";

static uint32_t data_crc = 0;
static uint32_t calc_crc = 0;
static uint32_t version = 0;
static uint32_t data_len = 0;

static esp_ota_handle_t update_handle = 0;
static const esp_partition_t *update_partition;

/* Handler to redirect incoming GET request for /index.html to / */
static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);  // Response body can be empty
    return ESP_OK;
}

/* Handler to respond with an icon file embedded in flash */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[]   asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);

    return ESP_OK;
}


struct CommandItemT
{
    sxmltok_t caption;    
    sxmltok_t logcaption;    
    sxmltok_t unit;
    sxmltok_t command;
    sxmltok_t writecommand;
    sxmltok_t length;
};

sxmltok_t TOK_NULL = {0};

struct OnAddTockenData
{
    const char* XmlSourse; 
    int XmlSourseLen;
    CommandItemT CommandItem;
    sxmltok_t StartToken;
    httpd_req_t *req;
}; 

void _OnAddSbsItem(OnAddTockenData* data);

bool _TokCmp(OnAddTockenData* data, const sxmltok_t& tok, const char* str)
{
    const char* tokStr = data->XmlSourse + tok.startpos;
    int tokLen = tok.endpos - tok.startpos;
    int strLen = strlen(str);

    if (tokLen != strLen)
        return false;

    bool res = strncmp(tokStr, str, tokLen) == 0;
    return res;
}

void _LogToken(OnAddTockenData* data, const char* tokName, const sxmltok_t& tok)
{
    if (tok.endpos == 0 || tok.startpos == 0)    
        return;
    char formatStr[16];
    sprintf(formatStr, "\t%%s - %%.%is\n\r", tok.endpos - tok.startpos);
    esp_log_write(ESP_LOG_INFO,    TAG,  formatStr, tokName, data->XmlSourse + tok.startpos);
}

void _OnAddTocken(const sxmltok_t* token, void* addTockenCallBackData)
{
    OnAddTockenData* data = (OnAddTockenData*)addTockenCallBackData; 

    switch (token->type)
    {
        case SXML_STARTTAG:
            data->StartToken = *token;
            //if (_TokCmp(data, *token, "sbsItem"))
            
            break;
        case SXML_ENDTAG:
            data->StartToken = TOK_NULL;
            if (_TokCmp(data, *token, "sbsItem"))
            {
                _OnAddSbsItem(data);
            }
            break;

        case SXML_CHARACTER:
            if (_TokCmp(data, data->StartToken, "caption"))
                data->CommandItem.caption = *token;
            if (_TokCmp(data, data->StartToken, "logcaption"))
                data->CommandItem.logcaption = *token;
            if (_TokCmp(data, data->StartToken, "unit"))
                data->CommandItem.unit = *token;
            if (_TokCmp(data, data->StartToken, "command"))
                data->CommandItem.command = *token;
            if (_TokCmp(data, data->StartToken, "writecommand"))
                data->CommandItem.writecommand = *token;
            if (_TokCmp(data, data->StartToken, "length"))
                data->CommandItem.length = *token;


            break;
        case SXML_CDATA:
        case SXML_INSTRUCTION:
        case SXML_DOCTYPE:
        case SXML_COMMENT:
            break;
    }

}

void _OnAddSbsItem(OnAddTockenData* data)
{
    //_LogToken(data, "caption", data->CommandItem.caption);
    //_LogToken(data, "logcaption", data->CommandItem.logcaption);
    //_LogToken(data, "command", data->CommandItem.command);
    //_LogToken(data, "unit", data->CommandItem.unit);

    //bq->DirectCommands(

    BqDevice* bq = BmsInterconnector::GetBqDevice();
    if (bq == NULL)
        return ;    



    //int32_t v = bq->readVoltage((DirectCommandAddress)((int)DirectCommandAddress::Cell1Voltage + i*2));

    char voltage[16];
    char id[16];
    char idSort[16];

    //sprintf(voltage, "%i.%03i", v/1000, v%1000);
    //sprintf(id, "%i", i+1);
    //sprintf(idSort, "%i", 16-i);
    //ESP_LOGI(TAG, "Cell %s %s V", id, voltage);
    
    httpd_resp_sendstr_chunk(data->req, "<tr><td id=\"");
    httpd_resp_sendstr_chunk(data->req, idSort);
    httpd_resp_sendstr_chunk(data->req, "\"><a>");
    httpd_resp_sendstr_chunk(data->req, id);
    httpd_resp_sendstr_chunk(data->req, "</a></td>");
    httpd_resp_sendstr_chunk(data->req, "<td>");
    httpd_resp_sendstr_chunk(data->req, voltage);
    httpd_resp_sendstr_chunk(data->req, "</td></tr>\n");

}







/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path. */
static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
{
    //const char *entrytype;

    //struct dirent *entry;
    //struct stat entry_stat;

    /*DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);*/

    //strlcpy(entrypath, dirpath, sizeof(entrypath));
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body onload=\"sortLogs()\">");

    extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
    extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

    // ESP_LOGI(TAG, "FIRMWARE_VER: %s", FIRMWARE_VER);
    // ESP_LOGI(TAG, "Ip4: %s", Ip4);
    // ESP_LOGI(TAG, "TCP_PORT: %d", TCP_PORT);
    // ESP_LOGI(TAG, "ConnectionType: %s", ConnectionType);
    // ESP_LOGI(TAG, "Ssid: %s", Ssid);

    char* longBuff = NULL;
    asprintf(
        &longBuff,
        "<div>"
            "<span><b>Firmware version:</b> %s</span><br>"
            "<span><b>IP & Port:</b> %s:%d</span><br>"
            "<span><b>Connection type, SSID:</b> %s, %s</span><br>"
            "<span><button type=\"button\" onclick=\"bmsUpdate()\">BMS update</button></span><br>"
        "</div>",
        FIRMWARE_VER,
        Ip4, TCP_PORT,
        ConnectionType, Ssid
    );
    // ESP_LOGI(TAG, "wifi_module_info: %s", wifi_module_info);
    if (longBuff)
    {
        httpd_resp_sendstr_chunk(req, (const char*) longBuff);
        free(longBuff);
    }

    /*if (!dir) {
        ESP_LOGI(TAG, "Failed to stat dir : %s", dirpath);
        int ret = mkdir(LOG_DIR, 0700);
        ESP_LOGI(TAG, "mkdir ret %d", ret);

        if (ret == ESP_FAIL) {
            httpd_resp_sendstr_chunk(req, "</body></html>");
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_OK;
        }
    }*/


    BqDevice* bq = BmsInterconnector::GetBqDevice();
    if (bq == NULL)
    {
        //httpd_resp_sendstr_chunk(req, "</body></html>");
        //httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;

    }

    asprintf(
        &longBuff,
        "<div>"
            "<span><b>Firmware version:</b> %s</span><br>"
            "<span><b>IP & Port:</b> %s:%d</span><br>"
            "<span><b>Connection type, SSID:</b> %s, %s</span><br>"
            "<span><button type=\"button\" onclick=\"bmsUpdate()\">BMS update</button></span><br>"
        "</div>",
        FIRMWARE_VER,
        Ip4, TCP_PORT,
        ConnectionType, Ssid
    );
    if (longBuff)
    {
        httpd_resp_sendstr_chunk(req, (const char*) longBuff);
        free(longBuff);
    }





    /*XMLDocument xmlDoc;
    if (XML_SUCCESS == xmlDoc.Parse((const char *)binary_Monitor_7695_0_36_bq76952_autogen_sbsx_start, 
                binary_Monitor_7695_0_36_bq76952_autogen_sbsx_end-binary_Monitor_7695_0_36_bq76952_autogen_sbsx_start))
    while (true)
    {
        auto sbsNode = xmlDoc.FirstChildElement("sbs");
        if (sbsNode == NULL)
            break;

        auto sbsItem = sbsNode->FirstChildElement("sbsItem");
        if (sbsItem == NULL)
            break;

        const char* sbsItemText = sbsItem->GetText();
        ESP_LOGI(TAG, "sbsItem: %s", sbsItemText);

        break;
    };*/

    httpd_resp_sendstr_chunk(req,
        "<table id=\"CellsTable\" class=\"fixed\" border=\"1\">"
        "<col width=\"300px\" /><col width=\"500px\" />"
        "<thead><tr><th>Cell</th><th>Voltage (volt)</th></tr></thead>"
        "<tbody id=\"tbd\">");

    extern const unsigned char binary_Monitor_7695_0_36_bq76952_autogen_sbsx_start[] asm("_binary_Monitor_7695_0_36_bq76952_autogen_sbsx_start");
    extern const unsigned char binary_Monitor_7695_0_36_bq76952_autogen_sbsx_end[]   asm("_binary_Monitor_7695_0_36_bq76952_autogen_sbsx_end");

    /* Parser object stores all data required for SXML to be reentrant */
    sxml_t parser;
    sxml_init (&parser);
    OnAddTockenData callBackData = 
    {
        .XmlSourse = (const char *)binary_Monitor_7695_0_36_bq76952_autogen_sbsx_start,
        .XmlSourseLen = binary_Monitor_7695_0_36_bq76952_autogen_sbsx_end-binary_Monitor_7695_0_36_bq76952_autogen_sbsx_start
    };
    sxmlerr_t err= sxml_parse (&parser, callBackData.XmlSourse, callBackData.XmlSourseLen, 0, 0, _OnAddTocken, &callBackData);    
    ESP_LOGI(TAG, "sxml_parse - %i (%i)", err, parser.ntokens);



        /*for (int i = 0; i < 16; i++)
        {
            int32_t v = bq->readVoltage((DirectCommandAddress)((int)DirectCommandAddress::Cell1Voltage + i*2));

            sprintf(voltage, "%i.%03i", v/1000, v%1000);
            sprintf(id, "%i", i+1);
            sprintf(idSort, "%i", 16-i);
            ESP_LOGI(TAG, "Cell %s %s V", id, voltage);
            
            httpd_resp_sendstr_chunk(req, "<tr><td id=\"");
            httpd_resp_sendstr_chunk(req, idSort);
            httpd_resp_sendstr_chunk(req, "\"><a>");
            httpd_resp_sendstr_chunk(req, id);
            httpd_resp_sendstr_chunk(req, "</a></td>");
            httpd_resp_sendstr_chunk(req, "<td>");
            httpd_resp_sendstr_chunk(req, voltage);
            httpd_resp_sendstr_chunk(req, "</td></tr>\n");
        };
        */



    /*
    int i = 1;
    char id[8];
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            continue;
        }
        
        entrytype = (entry->d_type == DT_DIR ? "directory" : "file");
        strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
        
        if (stat(entrypath, &entry_stat) == -1) {
            ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
            continue;
        }

        sprintf(entrysize, "%ld", entry_stat.st_size);
        sprintf(id, "%d", i);
        ESP_LOGI(TAG, "Found %s : %s (%s bytes)", entrytype, entry->d_name, entrysize);
        
        httpd_resp_sendstr_chunk(req, "<tr><td id=\"");
        httpd_resp_sendstr_chunk(req, id);
        httpd_resp_sendstr_chunk(req, "\"><a href=\"");
        httpd_resp_sendstr_chunk(req, req->uri);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        if (entry->d_type == DT_DIR) {
            httpd_resp_sendstr_chunk(req, "/");
        }
        httpd_resp_sendstr_chunk(req, "\">");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "</a></td>");
        httpd_resp_sendstr_chunk(req, "<td>");
        httpd_resp_sendstr_chunk(req, entrysize);
        httpd_resp_sendstr_chunk(req, "</td><th>");
        httpd_resp_sendstr_chunk(req, "<button type=\"button\" onclick=\"deleteLog('");
        httpd_resp_sendstr_chunk(req, req->uri);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "')\">Delete</button>");
        httpd_resp_sendstr_chunk(req, "</th></tr>\n");
        ++i;
    }
    closedir(dir);*/

    httpd_resp_sendstr_chunk(req, "</tbody></table>");
    httpd_resp_sendstr_chunk(req, "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

/* Handler to download a file kept on the server */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    if (filename[strlen(filename) - 1] == '/') {
        return http_resp_dir_html(req, LOG_DIR);
    }

    if (stat(filepath, &file_stat) == -1) {
        if (strcmp(filename, "/index.html") == 0) {
            return index_html_get_handler(req);
        } else if (strcmp(filename, "/favicon.ico") == 0) {
            return favicon_get_handler(req);
        }
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "fopen %s", filepath);
    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
               return ESP_FAIL;
           }
        }
    } while (chunksize != 0);

    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Handler to delete a file from the server */
static esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;
    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri  + sizeof("/delete") - 1, sizeof(filepath));
    if (!filename) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "File does not exist : %s", filename);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleting file : %s", filename);
    // StopFileLogging(filepath);
    unlink(filepath);

    /* Redirect onto root to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

/* Handler to delete all logs from the server */
static esp_err_t delete_logs_post_handler(httpd_req_t *req)
{
    const char *dirpath = LOG_DIR;
    /*char *entrypath = NULL;

    struct dirent *entry;
    struct stat entry_stat;*/

    /*DIR *dir = opendir(dirpath);
    //const size_t dirpath_len = strlen(dirpath);

    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return ESP_FAIL;
    }*/

    // StopLogging();
    /*while ((entry = readdir(dir)) != NULL) {
        asprintf(&entrypath, "%s/%s", dirpath, entry->d_name);
        if (stat(entrypath, &entry_stat) == -1) {
            ESP_LOGE(TAG, "Failed to stat %s", entry->d_name);
            continue;
        }
        
        ESP_LOGI(TAG, "Deleting %s", entrypath);
        unlink(entrypath);
    }
    closedir(dir);*/

    return http_resp_dir_html(req, dirpath);
}

/* Handler to bms */
static esp_err_t bms_update_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=> bms_update_post_handler");

    BqDevice* bq = BmsInterconnector::GetBqDevice();
    if (bq == NULL)
        return ESP_OK;    

    auto iv = bq->IsWorking();
    ESP_LOGI(TAG, "=>IsWorking - %i", iv);

    //ESP_LOGI(TAG, "=>Test");
    //BmsInterconnector::Test();

    //httpd_resp_send_chunk(req, NULL, 0);
    return http_resp_dir_html(req, NULL);
}

static esp_err_t ota_write_pre_encrypted(unsigned char *buf, int size)
{
	DecryptBlock(buf, size);
    esp_err_t err = esp_ota_write(update_handle, buf, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed - %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < size; ++i)
    {
        if (data_len > 0)
        {
            calc_crc = Crc32Update(buf[i]);
            --data_len;
        }
    }
    return ESP_OK;
}

static esp_err_t ota_write_pre_encrypted_start(unsigned char *buf, int size)
{
    if (size < 20 || memcmp(buf, ".em2.", 5) != 0) {
        ESP_LOGE(TAG, "file extension should be .em2");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&version, (buf + 7), 4);
    memcpy(&data_crc, (buf + 11), 4);
    memcpy(&data_len, (buf + 15), 4);

    DecryptInit();
    Crc32Init();

    return ota_write_pre_encrypted(buf + 20, size - 20);
}

static esp_err_t ota_write_pre_encrypted_finish()
{
	DestroyKey();
    if (data_crc != calc_crc) {
    	ESP_LOGE(TAG, "CRC value is invalid, file was corrupted!");
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

static esp_err_t ota_init()
{
    update_partition = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *running_partition = esp_ota_get_running_partition();

    if (update_partition == NULL) {
        ESP_LOGE(TAG, "update_partition is NULL");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing partition: type %d, subtype %d, offset 0x%08x",
        update_partition-> type, update_partition->subtype, update_partition->address);
    ESP_LOGI(TAG, "Running partition: type %d, subtype %d, offset 0x%08x\n",
        running_partition->type, running_partition->subtype, running_partition->address);

    esp_err_t err = ESP_OK;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

/* Handler to send file for firmware update */
static esp_err_t ota_post_handler(httpd_req_t *req)
{
    esp_err_t err;
    char buf[256];
    int ret, remaining = req->content_len;
    bool invalid_crc = false;
    bool invalid_file_ext = false;

    if (ota_init() != ESP_OK) {
        goto return_failure;
    }

    ESP_LOGI(TAG, "Receiving file of %d bytes", remaining);
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "HTTPD_SOCK_ERR_TIMEOUT");
                continue;
            }

            ESP_LOGE(TAG, "httpd_req_recv failed");
            goto return_failure;
        }

        if (remaining == req->content_len) {
            if (ota_write_pre_encrypted_start((unsigned char*) buf, ret) != ESP_OK) {
                invalid_file_ext = true;
                goto return_failure;
            }

            remaining -= ret;
            continue;
        }

        if (ota_write_pre_encrypted((unsigned char*) buf, ret) != ESP_OK) {
            goto return_failure;
        }
        
        remaining -= ret;
    }
    
    ESP_LOGI(TAG, "Receiving done");
    if (ota_write_pre_encrypted_finish() != ESP_OK) {
        invalid_crc = true;
        goto return_failure;
    }

    if ((err = esp_ota_end(update_handle)) == ESP_OK && (err = esp_ota_set_boot_partition(update_partition)) == ESP_OK) {
        ESP_LOGI(TAG, "OTA success! Rebooting...");
        fflush(stdout);

        httpd_resp_set_status(req, HTTPD_200);
        httpd_resp_send(req, NULL, 0);

        vTaskDelay(RESET_PAUSE / portTICK_RATE_MS);
        esp_restart();

        return ESP_OK;
    }
    ESP_LOGI(TAG, "OTA End failed (%s)!", esp_err_to_name(err));

    return_failure:
        if (update_handle) {
            esp_ota_abort(update_handle);
        }

        if (invalid_crc) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "CRC value is invalid, file was corrupted!");
            return ESP_FAIL;
        }

        if (invalid_file_ext) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File extension must be '.em2'!");
            return ESP_FAIL;
        }

        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal server error!");
        return ESP_FAIL;
}

/* Function to start the file server */
esp_err_t start_file_server(const char *base_path)
{
    static struct file_server_data *server_data = NULL;

    if (!base_path || strcmp(base_path, MOUNT_POINT) != 0) {
        ESP_LOGE(TAG, "File server presently supports only '%s' as base path", MOUNT_POINT);
        return ESP_ERR_INVALID_ARG;
    }
    if (server_data) {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    server_data = (file_server_data *)calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start file server!");
        return ESP_FAIL;
    }

    httpd_uri_t page_download = {
        .uri       = "/*",
        .method    = HTTP_GET,
        .handler   = download_get_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &page_download);

    httpd_uri_t ota_post = {
        .uri       = "/ota",
        .method    = HTTP_POST,
        .handler   = ota_post_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &ota_post);

    httpd_uri_t log_delete = {
        .uri       = "/delete/*",
        .method    = HTTP_POST,
        .handler   = delete_post_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &log_delete);

    httpd_uri_t logs_delete = {
        .uri       = "/deleteLogs",
        .method    = HTTP_POST,
        .handler   = delete_logs_post_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &logs_delete);

    httpd_uri_t bms_update = {
        .uri       = "/bms_update",
        .method    = HTTP_POST,
        .handler   = bms_update_post_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &bms_update);

    return ESP_OK;
}