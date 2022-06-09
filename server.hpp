#ifndef SERVER_HEADER
#define SERVER_HEADER

enum
{
    current_max_lcount = 16,
    buffsize = 1024,
};

class FdHandler
{
    int fd;

public:
    FdHandler(int fd_d) : fd(fd_d) {}
    virtual ~FdHandler();

    int GetFd() const { return fd; }
    bool WantRead() const { return true; }
    bool WantWrite() const { return false; }
    virtual void Handle(bool r, bool w) = 0;
};

class EventHandler
{
    FdHandler **fd_array;
    int fd_array_len;
    int max_fd;
    bool quit_flag;

public:
    EventHandler() : fd_array(0), quit_flag(true) {}
    ~EventHandler();

    void Add(FdHandler *h);
    void Remove(FdHandler *h);

    void Run();
    void BreakLoop() { quit_flag = false; }
};

/////////////////////////////////////////////////////////////

class ChatServer;

class ChatSession : public FdHandler
{
    friend class ChatServer;

    char buffer[buffsize];
    int buf_used;
    bool quit_flag;

    char *name;

    ChatServer *the_master;

    ChatSession(ChatServer *the_master_r, int fd);
    virtual ~ChatSession();
    virtual void Handle(bool r, bool w);
    bool HandleLine(const char *str);
    void QuitFromChat();

public:
    void Send(const char *msg);
    void Read();
};

class ChatServer : public FdHandler
{
    EventHandler *handler;
    struct item
    {
        ChatSession *s;
        item *next;
    };
    item *first;
    ChatServer(EventHandler *handler_h, int fd);

public:
    virtual ~ChatServer();
    static ChatServer *Start(EventHandler *handler_h, int port);
    void RemoveSession(ChatSession *s);
    void SendTo(const char *msg, const char *to_name, ChatSession *from);
    void SendAll(const char *msg, ChatSession *except = 0);

private:
    virtual void Handle(bool r, bool w);
};

#endif
