#include "header/utils.h"
#include <stdio.h>
#include <string.h>

int run_process_with_input(
    const char* executable,
    const char* input_text,
    char* output_buffer,
    DWORD output_buffer_size
) {
    HANDLE stdin_read,  stdin_write;
    HANDLE stdout_read, stdout_write;
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&stdin_read,  &stdin_write,  &sa, 0)) return -1;
    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) return -1;

    SetHandleInformation(stdin_write,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_read,  HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {0};
    si.cb          = sizeof(STARTUPINFOA);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdInput   = stdin_read;
    si.hStdOutput  = stdout_write;
    si.hStdError   = stdout_write;

    PROCESS_INFORMATION pi = {0};

    if (!CreateProcessA(
        NULL, (LPSTR)executable,
        NULL, NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL, NULL,
        &si, &pi
    )) {
        return -1;
    }

    CloseHandle(stdout_write);
    CloseHandle(stdin_read);

    if (input_text && strlen(input_text) > 0) {
        DWORD written;
        WriteFile(stdin_write, input_text, (DWORD)strlen(input_text), &written, NULL);
    }
    CloseHandle(stdin_write);

    if (output_buffer && output_buffer_size > 0) {
        DWORD bytes_read;
        DWORD total_read = 0;
        while (total_read < output_buffer_size - 1) {
            if (!ReadFile(stdout_read, output_buffer + total_read,
                          output_buffer_size - 1 - total_read,
                          &bytes_read, NULL) || bytes_read == 0) {
                break;
            }
            total_read += bytes_read;
        }
        output_buffer[total_read] = '\0';
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(stdout_read);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exit_code == 0) ? 0 : -1;
}
