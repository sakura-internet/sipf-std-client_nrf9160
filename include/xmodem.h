#ifndef _XMODEM_H_
#define _XMODEM_H_

enum xmodem_recv_ret
{
    XMODEM_RECV_RET_OK,
    XMODEM_RECV_RET_FINISHED,
    XMODEM_RECV_RET_CANCELED,
    XMODEM_RECV_RET_RETRY,
    XMODEM_RECV_RET_DUP,
    XMODEM_RECV_RET_FAILED = -1,
    XMODEM_RECV_RET_TIMEOUT = -2,
};

void XmodemBegin(void);
void XmodemEnd(void);

int XmodemReceiveStart(void);
enum xmodem_recv_ret XmodemReceiveBlock(uint8_t *bn, uint8_t *block, int time_out);
int XmodemReceiveCancel(void);

int XmodemReceiveReqNextBlock(void);
int XmodemReceiveReqCurrentBlock(void);
#endif