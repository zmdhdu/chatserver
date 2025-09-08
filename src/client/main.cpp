#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <functional>
using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <atomic>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

// 记录当前系统登录的用户信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;

// 控制主菜单页面程序
bool isMainMenuRunning = false;

// 用于读写线程之间的通信
sem_t rwsem;
// 记录登录状态
atomic_bool g_isLoginSuccess{false};

// 接收线程
void readTaskHandler(int clientfd);
// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime();
// 主聊天页面程序
void mainMenu(int);
// 显示当前登录成功用户的基本信息
void showCurrentUserData();

// 聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 8888" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    // 填写client需要连接的server信息ip+port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // client和server进行连接
    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    // 初始化读写线程通信用的信号量
    sem_init(&rwsem, 0, 0);

    // 连接服务器成功，启动接收子线程
    std::thread readTask(readTaskHandler, clientfd); // pthread_create
    readTask.detach();                               // pthread_detach

    // main线程用于接收用户输入，负责发送数据
    for (;;)
    {
        // 显示首页面菜单 登录、注册、退出
        cout << "========================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "========================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;
        cin.get(); // 读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: // login业务
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid:";
            cin >> id;
            cin.get(); // 读掉缓冲区残留的回车
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            g_isLoginSuccess = false;

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send login msg error:" << request << endl;
            }

            sem_wait(&rwsem); // 等待信号量，由子线程处理完登录的响应消息后，通知这里

            if (g_isLoginSuccess)
            {
                // 进入聊天主菜单页面
                isMainMenuRunning = true;
                mainMenu(clientfd);
            }
        }
        break;
        case 2: // register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send reg msg error:" << request << endl;
            }

            sem_wait(&rwsem); // 等待信号量，子线程处理完注册消息会通知
        }
        break;
        case 3: // quit业务
            close(clientfd);
            sem_destroy(&rwsem);
            exit(0);
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }

    return 0;
}

void doRegResponse(json &responsejs)
{
    int err = responsejs.value("errno", -1);
    if (err != 0)
    {
        std::cerr << responsejs.value("errmsg", "register error") << std::endl;
        return;
    }
    int id = responsejs.value("id", 0);
    std::cout << "name register success, userid is " << id << ", do not forget it!" << std::endl;
}

// 处理登录的响应逻辑
void doLoginResponse(json &responsejs)
{
    int err = responsejs.value("errno", -1);
    if (err != 0)
    {
        std::cerr << responsejs.value("errmsg", "login failed") << std::endl;
        g_isLoginSuccess = false;
        return;
    }

    // id/name
    if (!responsejs.contains("id") || !responsejs["id"].is_number_integer() || !responsejs.contains("name") || !responsejs["name"].is_string())
    {
        std::cerr << "[login] missing 'id' or 'name' in ack" << std::endl;
        g_isLoginSuccess = false;
        return;
    }

    g_currentUser.setId(responsejs["id"].get<int>());
    g_currentUser.setName(responsejs["name"].get<std::string>());

    // 好友列表（可选）
    g_currentUserFriendList.clear();
    if (responsejs.contains("friends") && responsejs["friends"].is_array())
    {
        for (auto &it : responsejs["friends"])
        {
            // 你的协议是数组里放的是序列化后的 string，这里兼容两种写法：
            json js;
            try
            {
                if (it.is_string())
                    js = json::parse(it.get<std::string>());
                else if (it.is_object())
                    js = it;
                else
                    continue;
            }
            catch (...)
            {
                continue;
            }

            User user;
            user.setId(js.value("id", 0));
            user.setName(js.value("name", ""));
            user.setState(js.value("state", ""));
            g_currentUserFriendList.push_back(user);
        }
    }

    // 群组列表（可选）
    g_currentUserGroupList.clear();
    if (responsejs.contains("groups") && responsejs["groups"].is_array())
    {
        for (auto &g : responsejs["groups"])
        {
            json grpjs;
            try
            {
                if (g.is_string())
                    grpjs = json::parse(g.get<std::string>());
                else if (g.is_object())
                    grpjs = g;
                else
                    continue;
            }
            catch (...)
            {
                continue;
            }

            Group group;
            group.setId(grpjs.value("id", 0));
            group.setName(grpjs.value("groupname", ""));
            group.setDesc(grpjs.value("groupdesc", ""));

            if (grpjs.contains("users") && grpjs["users"].is_array())
            {
                for (auto &u : grpjs["users"])
                {
                    json js;
                    try
                    {
                        if (u.is_string())
                            js = json::parse(u.get<std::string>());
                        else if (u.is_object())
                            js = u;
                        else
                            continue;
                    }
                    catch (...)
                    {
                        continue;
                    }

                    GroupUser user;
                    user.setId(js.value("id", 0));
                    user.setName(js.value("name", ""));
                    user.setState(js.value("state", ""));
                    user.setRole(js.value("role", ""));
                    group.getUsers().push_back(user);
                }
            }
            g_currentUserGroupList.push_back(group);
        }
    }

    // 离线消息（可选）
    if (responsejs.contains("offlinemsg") && responsejs["offlinemsg"].is_array())
    {
        for (auto &m : responsejs["offlinemsg"])
        {
            json js;
            try
            {
                if (m.is_string())
                    js = json::parse(m.get<std::string>());
                else if (m.is_object())
                    js = m;
                else
                    continue;
            }
            catch (...)
            {
                continue;
            }

            int mt = js.value("msgid", -1);
            if (mt == ONE_CHAT_MSG)
            {
                std::cout << js.value("time", "") << " ["
                          << js.value("id", 0) << "]"
                          << js.value("name", "") << " said: "
                          << js.value("msg", "") << std::endl;
            }
            else if (mt == GROUP_CHAT_MSG)
            {
                std::cout << "群消息[" << js.value("groupid", 0) << "]:"
                          << js.value("time", "") << " ["
                          << js.value("id", 0) << "]"
                          << js.value("name", "") << " said: "
                          << js.value("msg", "") << std::endl;
            }
        }
    }

    showCurrentUserData();
    g_isLoginSuccess = true;
}

// 子线程 - 接收线程
void readTaskHandler(int clientfd)
{
    for (;;)
    {
        char buffer[2048] = {0}; // 稍微大一点
        int len = recv(clientfd, buffer, sizeof(buffer), 0);
        if (len <= 0)
        {
            close(clientfd);
            exit(-1);
        }

        json js;
        try
        {
            js = json::parse(buffer);
        }
        catch (const std::exception &e)
        {
            std::cerr << "[recv] bad json: " << e.what() << " | raw=" << buffer << std::endl;
            continue; // 丢弃本条
        }

        std::cerr << "[recv] " << js.dump() << std::endl; // 调试必备：看到服务端到底发了啥

        // 安全取 msgid
        int msgtype = js.value("msgid", -1);
        if (msgtype == -1)
        {
            std::cerr << "[recv] missing or invalid 'msgid'" << std::endl;
            continue;
        }

        if (msgtype == ONE_CHAT_MSG)
        {
            std::cout << js.value("time", "")
                      << " [" << js.value("id", 0) << "]"
                      << js.value("name", "")
                      << " said: " << js.value("msg", "") << std::endl;
            continue;
        }

        if (msgtype == GROUP_CHAT_MSG)
        {
            std::cout << "群消息[" << js.value("groupid", 0) << "]:"
                      << js.value("time", "") << " ["
                      << js.value("id", 0) << "]"
                      << js.value("name", "") << " said: "
                      << js.value("msg", "") << std::endl;
            continue;
        }

        if (msgtype == LOGIN_MSG_ACK)
        {
            doLoginResponse(js);
            sem_post(&rwsem);
            continue;
        }

        if (msgtype == REG_MSG_ACK)
        {
            doRegResponse(js);
            sem_post(&rwsem);
            continue;
        }
    }
}

// 显示当前登录成功用户的基本信息
void showCurrentUserData()
{
    cout << "======================login user======================" << endl;
    cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "----------------------friend list---------------------" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "----------------------group list----------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState()
                     << " " << user.getRole() << endl;
            }
        }
    }
    cout << "======================================================" << endl;
}

// "help" command handler
void help(int fd = 0, string str = "");
// "chat" command handler
void chat(int, string);
// "addfriend" command handler
void addfriend(int, string);
// "creategroup" command handler
void creategroup(int, string);
// "addgroup" command handler
void addgroup(int, string);
// "groupchat" command handler
void groupchat(int, string);
// "loginout" command handler
void loginout(int, string);

// 系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令，格式help"},
    {"chat", "一对一聊天，格式chat:friendid:message"},
    {"addfriend", "添加好友，格式addfriend:friendid"},
    {"creategroup", "创建群组，格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组，格式addgroup:groupid"},
    {"groupchat", "群聊，格式groupchat:groupid:message"},
    {"loginout", "注销，格式loginout"}};

// 注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}};

// 主聊天页面程序
void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command; // 存储命令
        int idx = commandbuf.find(":");
        if (-1 == idx)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        // 调用相应命令的事件处理回调，mainMenu对修改封闭，添加新功能不需要修改该函数
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx)); // 调用命令处理方法
    }
}

// "help" command handler
void help(int, string)
{
    cout << "show command list >>> " << endl; 
    for (auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}
// "addfriend" command handler
void addfriend(int clientfd, string str)
{
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}
// "chat" command handler
void chat(int clientfd, string str)
{
    int idx = str.find(":"); // friendid:message
    if (-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send chat msg error -> " << buffer << endl;
    }
}
// "creategroup" command handler  groupname:groupdesc
void creategroup(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}
// "addgroup" command handler
void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addgroup msg error -> " << buffer << endl;
    }
}
// "groupchat" command handler   groupid:message
void groupchat(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send groupchat msg error -> " << buffer << endl;
    }
}
// "loginout" command handler
void loginout(int clientfd, string)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send loginout msg error -> " << buffer << endl;
    }
    else
    {
        isMainMenuRunning = false;
    }
}

// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}