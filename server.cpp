#include "server.hpp"
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include "server.hpp"

static const char join_msg[] = "joined to the chat.";
static const char left_msg[] = "left the chat.";
static const char greetings_msg[] =
    "Welcome to the chat,\nplease write a name: ";
static const char changed_name[] = "changed the name.";
static const char private_message_from[] = "Private message from ";
static const char name_without_spaces_msg[] =
    "Write a name without spaces and '/'\n";

FdHandler::~FdHandler()
{
    if (fd)
        close(fd);
}

///////////////////////////////////////////

EventHandler::~EventHandler()
{
    if (fd_array)
        delete[] fd_array;
}

void EventHandler::Add(FdHandler *h)
{
    int fd = h->GetFd();
    int i;
    if (!fd_array)
    {
        fd_array_len =
            fd > current_max_lcount - 1 ? fd + 1 : current_max_lcount;
        fd_array = new FdHandler *[fd_array_len];
        for (i = 0; i < fd_array_len; i++)
            fd_array[i] = 0;
        max_fd = -1;
    }
    if (fd_array_len <= fd)
    {
        FdHandler **tmp = new FdHandler *[fd + 1];
        for (i = 0; i <= fd; i++)
            tmp[i] = i < fd_array_len ? fd_array[i] : 0;
        fd_array_len = fd + 1;
        delete[] fd_array;
        fd_array = tmp;
    }
    if (fd > max_fd)
        max_fd = fd;
    fd_array[fd] = h;
}

void EventHandler::Remove(FdHandler *h)
{
    int fd = h->GetFd();
    if (fd >= fd_array_len || h != fd_array[fd])
        return;
    fd_array[fd] = 0;
    if (fd == max_fd)
    {
        while (max_fd >= 0 && !fd_array[max_fd])
            max_fd--;
    }
}

void EventHandler::Run()
{
    do
    {
        int i, res;
        fd_set rds, wrs;
        FD_ZERO(&rds);
        FD_ZERO(&wrs);
        for (i = 0; i <= max_fd; i++)
        {
            if (fd_array[i])
            {
                if (fd_array[i]->WantRead())
                    FD_SET(fd_array[i]->GetFd(), &rds);
                if (fd_array[i]->WantWrite())
                    FD_SET(fd_array[i]->GetFd(), &wrs);
            }
        }
        res = select(max_fd + 1, &rds, &wrs, 0, 0);
        if (res <= 0)
        {
            quit_flag = false;
            return;
        }
        for (i = 0; i <= max_fd; i++)
        {
            if (fd_array[i])
            {
                bool r = FD_ISSET(fd_array[i]->GetFd(), &rds);
                bool w = FD_ISSET(fd_array[i]->GetFd(), &wrs);
                if (r || w)
                    fd_array[i]->Handle(r, w);
            }
        }
    } while (quit_flag);
}

//////////////////////////////////////////////

ChatServer::ChatServer(EventHandler *handler_h, int fd)
    : FdHandler(fd), handler(handler_h), first(0)
{
    handler_h->Add(this);
}

ChatServer::~ChatServer()
{
    while (first)
    {
        item *tmp = first;
        first = first->next;
        handler->Remove(tmp->s);
        delete tmp->s;
        delete tmp;
    }
}

ChatServer *ChatServer::Start(EventHandler *handler_h, int port)
{
    int fd_s = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == fd_s)
        return 0;
    int opt = 1;
    setsockopt(fd_s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (-1 == bind(fd_s, (struct sockaddr *)&addr, sizeof(addr)))
        return 0;

    if (-1 == listen(fd_s, current_max_lcount))
        return 0;

    return new ChatServer(handler_h, fd_s);
}

void ChatServer::RemoveSession(ChatSession *s)
{
    handler->Remove(s);
    item **p;
    for (p = &first; *p; p = &((*p)->next))
    {
        if ((*p)->s == s)
        {
            item *tmp = *p;
            *p = tmp->next;
            delete tmp->s;
            delete tmp;
            return;
        }
    }
}

void ChatServer::SendAll(const char *msg, ChatSession *except)
{
    item *p;
    for (p = first; p; p = p->next)
        if (p->s != except)
            p->s->Send(msg);
}

void ChatServer::SendTo(const char *msg, const char *to_name)
{
    item *p;
    for (p = first; p; p = p->next)
    {
        if (strcmp(to_name, p->s->name) == 0)
        {
            char *tomsg =
                new char[sizeof(sizeof(private_message_from) + 4 +
                                strlen(msg) + strlen(p->s->name))];
            sprintf(tomsg, "%s%s: %s\n",
                    private_message_from, p->s->name, msg);
            p->s->Send(tomsg);
            delete[] tomsg;
            break;
        }
    }
}

void ChatServer::Handle(bool r, bool w)
{
    if (!r)
        return;
    struct sockaddr_in addr;
    socklen_t len;
    int fd_d = accept(GetFd(), (struct sockaddr *)&addr, &len);
    if (-1 == fd_d)
        return;

    item *p = new item;
    p->next = first;
    p->s = new ChatSession(this, fd_d);
    first = p;

    handler->Add(p->s);
}

ChatSession::ChatSession(ChatServer *the_master_r, int fd)
    : FdHandler(fd), buf_used(0), quit_flag(false),
      name(0), the_master(the_master_r)
{
    Send("Enter your name.\nNote: write your name without spaces and '/': ");
}

ChatSession::~ChatSession()
{
    if (name)
        delete[] name;
}

void ChatSession::Handle(bool r, bool w)
{
    if (!r)
        return;

    Read();
}

void ChatSession::QuitFromChat()
{
    if (name)
    {
        int len = strlen(name);
        char *lmsg = new char[len + sizeof(left_msg) + 2];
        sprintf(lmsg, "%s%s\n", name, left_msg);
        the_master->SendAll(lmsg, this);
        delete[] lmsg;
    }
    the_master->RemoveSession(this);
}

void ChatSession::Read()
{
    int fd_d = GetFd();
    int rc = read(fd_d, buffer, sizeof(buffer));
    if (rc < 1)
    {
        QuitFromChat();
        return;
    }
    int i;

    char *msg = new char[rc];

    for (i = 0; i < rc; i++)
    {
        if (buffer[i] == 0 || buffer[i] == '\n' || buffer[i] == '\r')
            break;
        msg[i] = buffer[buf_used + i];
    }
    msg[i] = 0;
    quit_flag = HandleLine(msg);
    delete[] msg;
    bzero(buffer, buf_used + rc);
    buf_used = 0;
    if (quit_flag)
        QuitFromChat();
}

bool ChatSession::HandleLine(const char *str)
{
    int str_len = strlen(str);
    if (str[0] == '/')
    {
        // /s - send to
        // /q - quit

        int i, j;
        for (i = 0; str[i] && str[i] != ' '; i++)
        {
        }
        int command_len = i;
        char *cmd = new char[command_len + 1];
        for (i = 0; i < command_len; i++)
            cmd[i] = str[i];
        cmd[i] = 0;
        if (strcmp(cmd, "/s") == 0)
        {
            char *to_name = 0, *to_msg = 0;
            const char *ptr = strstr(str, "<");
            if (ptr && strstr(str, ">"))
            {
                j = 0;
                while (ptr[j + 1] != '>' && ptr[j + 1])
                    j++;
                to_name = new char[j + 1];
                to_msg = new char[str_len - 6 - j + 1];
                if (sscanf(str, "/s <%s> %s", to_name, to_msg) == EOF)
                    Send("Usage: /s <name to message> message....\n");
                else
                {
                    to_name[j] = 0;
                    ptr = strstr(str, "> ");
                    j = 0;
                    while (*(ptr + 2 + j))
                    {
                        to_msg[j] = ptr[2 + j];
                        j++;
                    }
                    to_msg[j] = 0;
                    the_master->SendTo(to_msg, to_name);
                }

                delete[] to_name;
                delete[] to_msg;
                if (!ptr[j + 1])
                    Send("Usage: /s <name to message> message....\n");
            }
            else
                Send("Usage: /s <name to message> message....\n");
        }
        else if (strcmp(cmd, "/q") == 0)
        {
            delete[] cmd;
            return true;
        }
        else
            Send("Unknown command...\n");
        delete[] cmd;
        return false;
    }
    if (!name)
    {
        if (strstr(str, " ") || strstr(str, "/"))
        {
            Send(name_without_spaces_msg);
            return false;
        }

        name = new char[str_len];
        strcpy(name, str);
        char *gmsg = new char[str_len + sizeof(join_msg) + 3];
        sprintf(gmsg, "%s %s\n", name, join_msg);
        the_master->SendAll(gmsg, this);
        delete[] gmsg;
    }
    else
    {
        char *smsg = new char[str_len + strlen(name) + 5];
        sprintf(smsg, "<%s> %s\n", name, str);
        the_master->SendAll(smsg);
        delete[] smsg;
    }
    return false;
}

void ChatSession::Send(const char *msg)
{
    write(GetFd(), msg, strlen(msg));
}