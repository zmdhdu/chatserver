#ifndef CHATSERVICE_H
#define CHATSERVICE_H
// 这个抽象接口类主要做业务主要为上方chatserver的读写回调函数
#include <muduo/net/TcpConnection.h>
#include <functional>
#include <unordered_map>
#include <mutex>
#include "redis.hpp"
#include "offlinemessagemodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
using namespace muduo;
using namespace muduo::net;
using namespace std;

#include "json.hpp"
#include "usermodel.hpp"
using json = nlohmann::json;

// 回调函数处理：表示处理消息的事件回调方法类型
using MsgHandler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp)>;


// 聊天服务器业务类
class ChatService
{
public:
    // 获取单例对象的接口函数
    static ChatService *instance();

    // 处理登录业务
    void login(const TcpConnectionPtr &conn, json &js, Timestamp);
    // 处理注册业务
    void reg(const TcpConnectionPtr &conn, json &js, Timestamp);
    // 一对一聊天业务
    void oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 添加好友业务
    void addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 创建群组业务
    void createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    //加入群组业务
    void addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 群组聊天业务
    void groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 处理注销业务
    void loginout(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 获取消息的处理器
    MsgHandler getHander(int msgid);
    // 服务器异常，业务重置方法
    void reset();
    // 处理客户端异常退出
    void clientCloseException(const TcpConnectionPtr &conn);
    // 从redis消息队列中获取订阅的消息
    void handleRedisSubscribeMessage(int, string);

private:
    ChatService();                                // 私有化构造函数
    unordered_map<int,MsgHandler> _msgHandlerMap; // 存储消息id和其对应的业务处理方法

    //存储 消息id和其对应的业务处理方法
    unordered_map<int, TcpConnectionPtr> _userConnMap;
    // 定义互斥锁保证 _userConnMap 线程安全
    mutex _connMutex;
    

    // 数据操作类对象
    UserModel _usermodel;
    OfflineMsgModel _offlineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

    // redis 操作对象
    Redis _redis;
};

#endif // CHATSERVICE_H