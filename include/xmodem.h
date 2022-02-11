#ifndef _XMODEM_H_
#define _XMODEM_H_

enum xmodem_recv_ret
{
    XMODEM_RECV_RET_OK,
    XMODEM_RECV_RET_FINISHED,
    XMODEM_RECV_RET_CANCELED,
    XMODEM_RECV_RET_RETRY,
};

int XmodemReceiveStart(void);
enum xmodem_recv_ret XmodemReceiveBlock(uint8_t *bn, uint8_t *block, int time_out);
int XmodemReceiveEnd(void);

#endif