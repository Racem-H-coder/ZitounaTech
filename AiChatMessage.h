#ifndef AICHATMESSAGE_H
#define AICHATMESSAGE_H

#include <QString>

struct AiChatMessage
{
    QString text;
    bool isUser = false;
    qint64 timestampMs = 0;
};

#endif // AICHATMESSAGE_H
