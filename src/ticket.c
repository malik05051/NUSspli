/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2024 V10lator <v10lator@myway.de>                    *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 3 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include <wut-fixups.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <ticket.h>

#include <crypto.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <ioQueue.h>
#include <keygen.h>
#include <list.h>
#include <localisation.h>
#include <menu/engineFrames.h>
#include <menu/filebrowser.h>
#include <menu/utils.h>
#include <state.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#include <mbedtls/sha1.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define TICKET_BUCKET "/vol/slc/sys/rights/ticket/apps/"

typedef struct
{
    uint8_t *start;
    size_t size;
} TICKET_SECTION;

typedef struct WUT_PACKED
{
    uint32_t unk01;
    uint32_t unk02;
    uint32_t unk03;
    uint32_t unk04;
    uint16_t unk05;
    WUT_UNKNOWN_BYTES(0x06);
    uint32_t unk06[8];
    WUT_UNKNOWN_BYTES(0x60);
} TICKET_HEADER_SECTION;
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x04, unk02);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x08, unk03);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x0C, unk04);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x10, unk05);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x18, unk06);
WUT_CHECK_SIZE(TICKET_HEADER_SECTION, 0x98);

// cert3 (XS0000000c) of a downloaded cetk starts at 0x350 - that is sizeof(TICKET)
// plus sizeof(TICKET_HEADER_SECTION):
#define CETK_CERT3_OFFSET (sizeof(TICKET) + sizeof(TICKET_HEADER_SECTION))

static const uint8_t magic_header[10] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
static uint8_t default_cert[sizeof(OTHER_PPKI_CERT)] = { 0xff };

static void generateHeader(FileType type, NUS_HEADER *out)
{
    OSBlockMove(out->magic_header, magic_header, 10, false);
    OSBlockMove(out->app, "NUSspli", sizeof("NUSspli") - 1, false);
    OSBlockMove(out->app_version, NUSSPLI_VERSION, sizeof(NUSSPLI_VERSION) - 1, false);

    if(type == FILE_TYPE_TIK)
        OSBlockMove(out->file_type, "Ticket", sizeof("Ticket") - 1, false);
    else
        OSBlockMove(out->file_type, "Certificate", sizeof("Certificate") - 1, false);

    out->sig_type = type == FILE_TYPE_TIK ? 0x00010004 : 0x00010003;
    out->meta_version = 0x01;
    osslBytes(out->rand_area, sizeof(out->rand_area));
}

bool generateTik(const char *path, const TMD *tmd)
{
    TICKET ticket;
    OSBlockSet(&ticket, 0x00, sizeof(TICKET));

    if(!generateKey(tmd->tid, ticket.key))
        return false;

    generateHeader(FILE_TYPE_TIK, &ticket.header);
    osslBytes(&ticket.ecdsa_pubkey, sizeof(ticket.ecdsa_pubkey));
    osslBytes(&ticket.ticket_id, sizeof(uint64_t));
    ticket.ticket_id &= 0x0000FFFFFFFFFFFF;
    ticket.ticket_id |= 0x0005000000000000;

    OSBlockMove(ticket.issuer, "Root-CA00000003-XS0000000c", sizeof("Root-CA00000003-XS0000000c") - 1, false);

    ticket.version = 0x01;
    ticket.tid = tmd->tid;
    ticket.title_version = tmd->title_version;
    ticket.property_mask = 0xFFFF;

    ticket.header_version = 0x0001;
    // Normal tickets don't need header sections but DLC needs one
    if(!isDLC(tmd->tid))
        ticket.total_hdr_size = 0x00000014;
    else
    {
        ticket.total_hdr_size = 0x000000AC;
        ticket.sect_hdr_offset = 0x00000014;
        ticket.num_sect_headers = 0x0001;
        ticket.num_sect_header_entry_size = 0x0014;
    }

    FSAFileHandle tik = openFile(path, "w", 0);
    if(tik == 0)
    {
        char err[FS_MAX_PATH + 256];
        sprintf(err, "%s\n%s", localise("Could not open path"), prettyDir(path));
        showErrorFrame(err);
        return false;
    }

    addToIOQueue(&ticket, 1, sizeof(TICKET), tik);

    if(isDLC(tmd->tid))
    {
        TICKET_HEADER_SECTION section;
        OSBlockSet(&section, 0x00, sizeof(TICKET_HEADER_SECTION));

        section.unk01 = 0x00000028;
        section.unk02 = 0x00000001;
        section.unk03 = 0x00000084;
        section.unk04 = 0x00000084;
        section.unk05 = 0x0003;
        for(int i = 0; i < 8; i++)
            section.unk06[i] = 0xFFFFFFFF;

        addToIOQueue(&section, 1, sizeof(TICKET_HEADER_SECTION), tik);
    }

    addToIOQueue(NULL, 0, 0, tik);
    return true;
}

static uint8_t *getDefaultCert()
{
    uint8_t *ret = NULL;
    if(default_cert[0] == 0xff)
    {
        RAMBUF *rambuf = allocRamBuf();
        if(rambuf != NULL)
        {
            if(downloadFile(DOWNLOAD_URL "000500101000400a/cetk", "OSv10 title.tik", NULL, FILE_TYPE_TIK | FILE_TYPE_TORAM, false, NULL, rambuf) == 0)
            {
                if(rambuf->size >= CETK_CERT3_OFFSET + sizeof(OTHER_PPKI_CERT))
                {
                    OSBlockMove(default_cert, rambuf->buf + CETK_CERT3_OFFSET, sizeof(OTHER_PPKI_CERT), false);
                    ret = default_cert;
                }
            }

            freeRamBuf(rambuf);
        }
    }
    else
        return default_cert;

    return ret;
}

static void *getCert(int id, const TMD *tmd)
{
    const uint8_t *ptr = (const uint8_t *)tmd;
    ptr += sizeof(TMD) + (sizeof(TMD_CONTENT) * tmd->num_contents);
    if(id == 0)
        ptr += sizeof(OTHER_PPKI_CERT);

    return (void *)ptr;
}

bool generateCert(const TMD *tmd, const TICKET *ticket, size_t ticketSize, const char *path)
{
    CETK cetk;

    if(ticketSize == 0)
    {

        OSBlockSet(&cetk, 0x00, sizeof(CETK));

        OSBlockMove(cetk.cert1.issuer, "Root", sizeof("Root") - 1, false);
        OSBlockMove(cetk.cert1.type, "CA00000003", sizeof("CA00000003") - 1, false);

        OSBlockMove(cetk.cert2.issuer, "Root-CA00000003", sizeof("Root-CA00000003") - 1, false);
        OSBlockMove(cetk.cert2.type, "CP0000000b", sizeof("CP0000000b") - 1, false);

        OSBlockMove(cetk.cert3.issuer, "Root-CA00000003", sizeof("Root-CA00000003") - 1, false);
        OSBlockMove(cetk.cert3.type, "XS0000000c", sizeof("XS0000000c") - 1, false);

        osslBytes(&cetk.cert1.sig, sizeof(cetk.cert1.sig));
        osslBytes(&cetk.cert1.cert, sizeof(cetk.cert1.cert));
        osslBytes(&cetk.cert2.sig, sizeof(cetk.cert2.sig));
        osslBytes(&cetk.cert2.cert, sizeof(cetk.cert2.cert));
        osslBytes(&cetk.cert3.sig, sizeof(cetk.cert3.sig));

        cetk.cert1.sig_type = 0x00010003;
        cetk.cert1.version = 0x00000001;
        cetk.cert1.unknown_01 = 0x00010001;

        cetk.cert2.sig_type = 0x00010004;
        cetk.cert2.version = 0x00000001;
        cetk.cert2.unknown_01 = 0x00010001;

        cetk.cert3.sig_type = 0x00010004;
        cetk.cert3.version = 0x00000001;
        cetk.cert3.unknown_01 = 0x00010001;

        // Overrite header
        OSBlockSet(&cetk, 0x00, sizeof(NUS_HEADER));
        generateHeader(FILE_TYPE_CERT, (NUS_HEADER *)&cetk);
    }
    else
    {
        const uint8_t *ptr;
        if(ticketSize >= CETK_CERT3_OFFSET + sizeof(OTHER_PPKI_CERT))
        {
            ptr = (uint8_t *)ticket;
            ptr += CETK_CERT3_OFFSET;
        }
        else
        {
            ptr = getDefaultCert();
            if(ptr == NULL)
                return generateCert(tmd, NULL, 0, path);
        }

        OSBlockMove(&cetk.cert1, getCert(0, tmd), sizeof(CA3_PPKI_CERT), false);
        OSBlockMove(&cetk.cert2, getCert(1, tmd), sizeof(OTHER_PPKI_CERT), false);
        OSBlockMove(&cetk.cert3, ptr, sizeof(OTHER_PPKI_CERT), false);
    }

    FSAFileHandle cert = openFile(path, "w", 0);
    if(cert == 0)
    {
        char err[FS_MAX_PATH + 256];
        sprintf(err, "%s\n%s", localise("Could not open path"), prettyDir(path));
        showErrorFrame(err);
        return false;
    }

    addToIOQueue(&cetk, 1, sizeof(CETK), cert);
    addToIOQueue(NULL, 0, 0, cert);
    return true;
}

static void browseFiles(char *out)
{
    const char *dir = fileBrowserMenu(false, false);
    if(dir)
    {
        strcpy(out, dir);
        MEMFreeToDefaultHeap((void *)dir);
    }
    else
        *out = '\0';
}

void generateFakeTicket()
{
    char dir[FS_MAX_PATH];
    TMD *tmd;
gftEntry:
    browseFiles(dir);
    if(*dir == '\0')
        return;

    tmd = getTmd(dir, false);
    if(tmd == NULL)
    {
        showErrorFrame(localise("Invalid title.tmd file!"));
        return;
    }

    ErrorOptions choice = showTicketConfirmDialog(tmd->tid);
    if(choice == 0) // The app stopped while the dialog was up
    {
        MEMFreeToDefaultHeap(tmd);
        return;
    }
    if(choice & B_RETURN)
    {
        MEMFreeToDefaultHeap(tmd);
        goto gftEntry;
    }

    showStatusFrame(localise("Generating fake ticket..."), NULL, false, -1);

    // fileBrowserMenu() leaves room for exactly this behind the folder it
    // hands out, so this only guards against a shorter way in later on:
    // never write the two names past the buffer.
    if(strlen(dir) + sizeof("title.cert") > FS_MAX_PATH)
    {
        debugPrintf("Path too long: %s", dir);
        goto gftFree;
    }

    strcat(dir, "title.");
    char *ptr = dir + strlen(dir);
    strcpy(ptr, "cert");
    if(!generateCert(tmd, NULL, 0, dir))
        goto gftFree;

    strcpy(ptr, "tik");
    if(!generateTik(dir, tmd))
        goto gftFree;

    showTicketGeneratedDialog(dir);

gftFree:
    MEMFreeToDefaultHeap(tmd);
}

void deleteTicket(uint64_t tid)
{
    LIST *ticketList = createList();
    if(ticketList == NULL)
        return;

    char path[FS_MAX_PATH];
    OSBlockMove(path, TICKET_BUCKET, sizeof(TICKET_BUCKET), false);

    char *inSentence = path + (sizeof(TICKET_BUCKET) - 1);
    FSADirectoryHandle dir;
    OSTime t = OSGetTime();
    FSError ret = FSAOpenDir(getFSAClient(), path, &dir);
    if(ret != FS_ERROR_OK)
    {
        debugPrintf("Error opening %s: %s", path, translateFSErr(ret));
        goto exit;
    }

    FSADirectoryEntry entry;
    FSADirectoryHandle dir2;
    char *fileName;
    void *file;
    size_t fileSize;
    TICKET *ticket;
    TICKET_SECTION *sec;
    bool found;
    uint8_t *fileEnd;
    uint8_t *ptr;
    // Loop through all the folder inside of the ticket bucket
    while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
    {
        if(!(entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 4)
        {
            debugPrintf("Sanity check failed on %s", entry.name);
            continue;
        }

        strcpy(inSentence, entry.name);
        ret = FSAOpenDir(getFSAClient(), path, &dir2);
        if(ret == FS_ERROR_OK)
        {
            strcat(inSentence, "/");
            fileName = inSentence + strlen(inSentence);
            // Loop through all the subfolders
            while(FSAReadDir(getFSAClient(), dir2, &entry) == FS_ERROR_OK)
            {
                // FSAReadDir never returns FS_STAT_FILE (0x01000000): the IOSU leaves
                // that bit clear for plain files (libiosuhax even has to OR it into its
                // own FSA results by hand), so the directory flag is tested negatively.
                if((entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 12)
                {
                    debugPrintf("Sanity check failed on %s", entry.name);
                    continue;
                }

                strcpy(fileName, entry.name);
                fileSize = readFile(path, &file);
                if(file != NULL)
                {
                    ticket = (TICKET *)file;
                    fileEnd = ((uint8_t *)file) + fileSize;
                    found = false;
                    // Loop through all the tickets inside of a file
                    while(true)
                    {
                        ptr = ((uint8_t *)ticket) + sizeof(TICKET);
                        if(ticket->total_hdr_size > 0x14)
                            ptr += ticket->total_hdr_size - 0x14;

                        // Ignore the ticket in case the title ID matches
                        if(ticket->tid == tid)
                        {
                            found = true;
                            debugPrintf("Ticket found at %s+0x%X", path, ((uint8_t *)ticket) - ((uint8_t *)file));
                        }
                        // Else remeber its offset and size
                        else
                        {
                            sec = MEMAllocFromDefaultHeap(sizeof(TICKET_SECTION));
                            if(sec)
                            {
                                sec->start = (uint8_t *)ticket;
                                sec->size = ptr - sec->start;

                                if(!addToListEnd(ticketList, sec))
                                {
                                    MEMFreeToDefaultHeap(sec);
                                    debugPrintf("Error allocating memory!");
                                    found = false;
                                    break;
                                }
                            }
                            else
                            {
                                debugPrintf("Error allocating memory!");
                                found = false;
                                break;
                            }
                        }

                        if(ptr == fileEnd)
                            break;
                        if(ptr > fileEnd)
                        {
                            debugPrintf("Filesize missmatch!");
                            found = false;
                            break;
                        }

                        ticket = (TICKET *)ptr;
                    }

                    // In case there was a matching ticket inside of the file either delete or recreate it with the remembered tickets only
                    if(found)
                    {
                        if(getListSize(ticketList) == 0)
                            FSARemove(getFSAClient(), path);
                        else
                        {
                            FSAFileHandle fh = openFile(path, "w", 0);
                            forEachListEntry(ticketList, sec)
                                addToIOQueue(sec->start, 1, sec->size, fh);

                            addToIOQueue(NULL, 0, 0, fh);
                        }
                    }

                    clearList(ticketList, true);
                    MEMFreeToDefaultHeap(file);
                }
                else if(fileSize == 0 && getFilesize(path) == 0) // readFile() also returns 0 on errors, only remove real zero byte files
                {
                    debugPrintf("Removing %s", path);
                    FSARemove(getFSAClient(), path);
                }
            }

            FSACloseDir(getFSAClient(), dir2);
        }
        else
            debugPrintf("Error opening %s: %s", path, translateFSErr(ret));
    }

    FSACloseDir(getFSAClient(), dir);
exit:
    t = OSGetTime() - t;
    addEntropy(&t, sizeof(OSTime));
    destroyList(ticketList, true);
}

bool hasTicket(uint64_t tid)
{
    char path[FS_MAX_PATH];
    OSBlockMove(path, TICKET_BUCKET, sizeof(TICKET_BUCKET), false);
    char *inSentence = path + (sizeof(TICKET_BUCKET) - 1);

    FSADirectoryHandle dir;
    if(FSAOpenDir(getFSAClient(), path, &dir) != FS_ERROR_OK)
        return false;

    // The same walk as deleteTicket(): folders of four characters, files of
    // twelve, every file a row of tickets.
    bool found = false;
    FSADirectoryEntry entry;
    FSADirectoryHandle dir2;
    while(!found && FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
    {
        if(!(entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 4)
            continue;

        strcpy(inSentence, entry.name);
        if(FSAOpenDir(getFSAClient(), path, &dir2) != FS_ERROR_OK)
            continue;

        strcat(inSentence, "/");
        char *fileName = inSentence + strlen(inSentence);
        while(!found && FSAReadDir(getFSAClient(), dir2, &entry) == FS_ERROR_OK)
        {
            if((entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 12)
                continue;

            strcpy(fileName, entry.name);
            void *file;
            size_t fileSize = readFile(path, &file);
            if(file == NULL)
                continue;

            uint8_t *fileEnd = ((uint8_t *)file) + fileSize;
            TICKET *ticket = (TICKET *)file;
            while(((uint8_t *)ticket) + sizeof(TICKET) <= fileEnd)
            {
                if(ticket->tid == tid)
                {
                    found = true;
                    break;
                }

                uint8_t *ptr = ((uint8_t *)ticket) + sizeof(TICKET);
                if(ticket->total_hdr_size > 0x14)
                    ptr += ticket->total_hdr_size - 0x14;

                ticket = (TICKET *)ptr;
            }

            MEMFreeToDefaultHeap(file);
        }

        FSACloseDir(getFSAClient(), dir2);
    }

    FSACloseDir(getFSAClient(), dir);
    return found;
}

// The file a ticket belongs in follows from the SHA-1 hash of its title ID
// (big endian, as in the ticket): the low nibble of byte 2 is the folder, the
// low five bits of byte 3 the file. This is not documented anywhere, it was
// read from the store of a 5.5.5E console, where all of its 95 title IDs sat
// in exactly that file. dumpTicketStore() checks it again on every console
// before addUpdateTicket() relies on it.
static void ticketPath(uint64_t tid, char *path, size_t size)
{
    uint8_t hash[20];
    mbedtls_sha1((const unsigned char *)&tid, sizeof(tid), hash);
    snprintf(path, size, TICKET_BUCKET "%04x/%08x.tik", hash[2] & 0x0F, hash[3] & 0x1F);
}

// The backup of the whole store and a list of where every ticket of it sits,
// taken before addUpdateTicket() changes anything. It reads the store the way
// deleteTicket() does and writes to the SD card only.
#define TICKET_LIST_SIZE (256 * 1024)

static void writeSdFile(const char *path, const void *data, size_t size)
{
    FSAFileHandle fh = openFile(path, "w", 0);
    if(fh == 0)
        return;

    if(size != 0)
        addToIOQueue(data, 1, size, fh);

    addToIOQueue(NULL, 0, 0, fh);
}

int dumpTicketStore(int *files, int *misplaced)
{
    char *list = MEMAllocFromDefaultHeap(TICKET_LIST_SIZE);
    if(list == NULL)
        return -1;

    size_t used = 0;
    int tickets = 0;
    *files = 0;
    *misplaced = 0;

    char path[FS_MAX_PATH];
    char backup[FS_MAX_PATH];
    char expected[FS_MAX_PATH];
    OSBlockMove(path, TICKET_BUCKET, sizeof(TICKET_BUCKET), false);
    char *inSentence = path + (sizeof(TICKET_BUCKET) - 1);

    FSADirectoryHandle dir;
    FSError err = FSAOpenDir(getFSAClient(), path, &dir);
    if(err != FS_ERROR_OK)
    {
        used += snprintf(list + used, TICKET_LIST_SIZE - used, "Error opening %s: %s\n", path, translateFSErr(err));
        goto write;
    }

    FSADirectoryEntry entry;
    FSADirectoryHandle dir2;
    while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
    {
        if(!(entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 4)
            continue;

        strcpy(inSentence, entry.name);
        if(FSAOpenDir(getFSAClient(), path, &dir2) != FS_ERROR_OK)
            continue;

        snprintf(backup, sizeof(backup), TICKET_BACKUP_DIR "%s", entry.name);
        createDirRecursive(backup);

        strcat(inSentence, "/");
        char *fileName = inSentence + strlen(inSentence);
        while(FSAReadDir(getFSAClient(), dir2, &entry) == FS_ERROR_OK)
        {
            // Directories are skipped by their flag, see deleteTicket().
            if((entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 12)
                continue;

            strcpy(fileName, entry.name);
            void *file;
            size_t fileSize = readFile(path, &file);
            if(file == NULL)
                continue;

            ++*files;
            snprintf(backup, sizeof(backup), TICKET_BACKUP_DIR "%s", inSentence);
            writeSdFile(backup, file, fileSize);

            if(used < TICKET_LIST_SIZE)
                used += snprintf(list + used, TICKET_LIST_SIZE - used, "%s (%u bytes)\n", inSentence, (unsigned int)fileSize);

            uint8_t *fileEnd = ((uint8_t *)file) + fileSize;
            TICKET *ticket = (TICKET *)file;
            while(((uint8_t *)ticket) + sizeof(TICKET) <= fileEnd)
            {
                uint8_t *ptr = ((uint8_t *)ticket) + sizeof(TICKET);
                if(ticket->total_hdr_size > 0x14)
                    ptr += ticket->total_hdr_size - 0x14;

                if(used < TICKET_LIST_SIZE)
                    used += snprintf(list + used, TICKET_LIST_SIZE - used, "  +0x%04X size %u title %016llX ticket %016llX version %u\n",
                        (unsigned int)(((uint8_t *)ticket) - ((uint8_t *)file)), (unsigned int)(ptr - (uint8_t *)ticket), ticket->tid, ticket->ticket_id, ticket->title_version);

                ticketPath(ticket->tid, expected, sizeof(expected));
                if(strcmp(path, expected) != 0)
                {
                    ++*misplaced;
                    if(used < TICKET_LIST_SIZE)
                        used += snprintf(list + used, TICKET_LIST_SIZE - used, "    not in the expected file %s\n", expected + (sizeof(TICKET_BUCKET) - 1));
                }

                ++tickets;
                if(ptr >= fileEnd)
                    break;

                ticket = (TICKET *)ptr;
            }

            MEMFreeToDefaultHeap(file);
        }

        FSACloseDir(getFSAClient(), dir2);
    }

    FSACloseDir(getFSAClient(), dir);

write:
    if(used > TICKET_LIST_SIZE)
        used = TICKET_LIST_SIZE;

    writeSdFile(TICKET_LIST_PATH, list, used);
    flushIOQueue();
    MEMFreeToDefaultHeap(list);
    return checkForQueueErrors() ? -1 : tickets;
}

// An update has a ticket anyone can download from the CDN, the one NUSspli
// installs updates with. Without it on the console, the Wii U Download
// Management downloads an update and then fails to install it (105-3683),
// and nn::nim::DownloadPatchTitleTicket did not add it on a 5.5.5E console.
//
// Only that public ticket is added, and only as it comes from the server:
// signed by Nintendo, for no console and no account in particular. The
// console checks the signature itself when it uses the ticket. The bytes are
// appended to the file the ticket belongs in, the tickets in there already
// stay as they are, and deleteTicket() takes it out again.
#define TICKET_ISSUER "Root-CA00000003-XS0000000c"

ADD_TICKET_RESULT addUpdateTicket(uint64_t tid)
{
    if(!isUpdate(tid) || hasTicket(tid))
        return ADD_TICKET_INVALID;

    RAMBUF *rambuf = allocRamBuf();
    if(rambuf == NULL)
        return ADD_TICKET_WRITE_FAILED;

    char path[FS_MAX_PATH];
    snprintf(path, sizeof(path), DOWNLOAD_URL "%016llx/cetk", tid);
    if(downloadFile(path, "title.tik", NULL, FILE_TYPE_TIK | FILE_TYPE_TORAM, false, NULL, rambuf) != 0)
    {
        freeRamBuf(rambuf);
        return ADD_TICKET_NOT_ON_SERVER;
    }

    // A cetk is the ticket followed by the certificates, only the ticket goes
    // into the store. Its size is worked out like in deleteTicket().
    const TICKET *ticket = (const TICKET *)rambuf->buf;
    size_t size = sizeof(TICKET);
    uint32_t sigType = 0;
    if(rambuf->size >= sizeof(TICKET))
    {
        if(ticket->total_hdr_size > 0x14)
            size += ticket->total_hdr_size - 0x14;

        OSBlockMove(&sigType, rambuf->buf, sizeof(sigType), false);
    }

    if(rambuf->size < sizeof(TICKET) || size > rambuf->size || size > 0x1000 || sigType != 0x00010004 || // RSA-2048 with SHA-256
        strncmp(ticket->issuer, TICKET_ISSUER, sizeof(ticket->issuer)) != 0 || ticket->tid != tid || ticket->device_id != 0 || ticket->account_id != 0)
    {
        freeRamBuf(rambuf);
        return ADD_TICKET_INVALID;
    }

    ticketPath(tid, path, sizeof(path));
    debugPrintf("Adding the ticket of %016llX to %s", tid, path);

    // Only append: "a" creates the file when there is none and never touches
    // the bytes already in it.
    FSAFileHandle fh = openFile(path, "a", 0);
    if(fh == 0)
    {
        freeRamBuf(rambuf);
        return ADD_TICKET_WRITE_FAILED;
    }

    addToIOQueue(rambuf->buf, 1, size, fh);
    addToIOQueue(NULL, 0, 0, fh);
    flushIOQueue();
    freeRamBuf(rambuf);

    if(checkForQueueErrors() || !hasTicket(tid))
        return ADD_TICKET_WRITE_FAILED;

    return ADD_TICKET_OK;
}

bool hasMagicHeader(const TICKET *ticket)
{
    for(size_t i = 0; i < sizeof(magic_header); ++i)
        if(ticket->header.magic_header[i] != magic_header[i])
            return false;

    return ticket->header.meta_version == 0x01;
}
