#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <vector>

using namespace muduo;
using namespace std;
// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 注册消息以及对应的 Handler 回调操作
ChatService::ChatService()
{
    // 绑定之后即可调用回调函数
    _msgHandlerMap.insert({LOGIN_MSG,std::bind(&ChatService::login,this,placeholders::_1,placeholders::_2,placeholders::_3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout,this,placeholders::_1,placeholders::_2,placeholders::_3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, placeholders::_1, placeholders::_2, placeholders::_3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, placeholders::_1, placeholders::_2, placeholders::_3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(& ChatService::addFriend, this, placeholders::_1, placeholders::_2, placeholders::_3)});
    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, placeholders::_1, placeholders::_2, placeholders::_3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, placeholders::_1, placeholders::_2, placeholders::_3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, placeholders::_1, placeholders::_2, placeholders::_3)});

    // 链接redis 服务器
    if (_redis.connect())
    {
        // 设置上报消息回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage,this,_1,_2) );
    }

} // 私有化构造函数
  // 服务器异常，业务重置方法

void ChatService::reset()
{
    // 把online 状态的所有用户，设置为offline
    _usermodel.resetState();
}

// 获取消息的处理器
MsgHandler ChatService::getHander(int msgid)
{
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it=_msgHandlerMap.find(msgid);
    if (it==_msgHandlerMap.end())
    {
        // 返回一个默认的处理器 ， 空操作
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp)
        {
            LOG_ERROR << "msgid:" << msgid << "can not find handler!";
        };
        
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

// 处理登录业务
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    LOG_INFO << " do login service!!!";
    int id = js["id"];
    string password = js["password"];

    User user = _usermodel.query(id);
    
    if(user.getId() != -1 && user.getPassword() == password)
    {
        if (user.getState() =="online")
        {
            // 已经登录成功
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this account is using ，input annother";
            conn->send(response.dump());
        }
        else
        { // 登录成功
            {
                lock_guard<mutex> lock(_connMutex);
                // 记录 连接中的用户信息，让服务器可以主动推送给 其他客户端
                _userConnMap.insert({id, conn});
            }
            // id用户登录成功后，向redis订阅channel（id）
            _redis.subscribe(id);

            // 登录成功  ： 更新用户状态信息 state offline =>online
            user.setState("online");
            _usermodel.updateState(user);
            json response;// 回复的响应消息
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            // 用户登录成功后会查询 数据表是否有响应消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                // 读取离线消息后，把该用户对应的所有离线消息都删除掉
                _offlineMsgModel.remove(id);
            }
            // 查询该用户的好友信息并返回
            vector<User>userVec = _friendModel.query(id);
            if(!userVec.empty())
            {
                vector<string> vec2;
                for (User& user :userVec)
                {
                    json js; // 存储到 json对象中，并发送
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }
            // 查询该用户的群组信息并返回（与客户端协议保持一致）
            vector<Group> groups = _groupModel.queryGroups(id); // 需要实现，见下方
            if (!groups.empty())
            {
                vector<string> groups_out;
                for (auto &g : groups)
                {
                    json gobj;
                    gobj["id"] = g.getId();
                    gobj["groupname"] = g.getName();
                    gobj["groupdesc"] = g.getDesc();

                    // 组内成员：序列化为字符串数组（客户端当前逻辑期望 string）
                    vector<string> members_out;
                    for (auto &m : g.getUsers())
                    {
                        json jm;
                        jm["id"] = m.getId();
                        jm["name"] = m.getName();
                        jm["state"] = m.getState();
                        jm["role"] = m.getRole();
                        members_out.push_back(jm.dump());
                    }
                    gobj["users"] = members_out;

                    groups_out.push_back(gobj.dump());
                }
                response["groups"] = groups_out;
            }

            conn->send(response.dump());// 发送给客户端
        }
    }
    else
    {   
        // 该用户不存在 或密码 错误登陆失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "id or password is invalid";
        conn->send(response.dump());
    }
}
// 处理注册业务
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPassword(pwd);// orm 映射
    bool state = _usermodel.insert(user); // 对 Uesr 表操作 结果判断
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
    
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid);

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    _usermodel.updateState(user);
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;

    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); it++)
        {
            if (it->second==conn)
            {
                // 断开，则从map变删除
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
        // 用户注销，相当于下线，在redis中取消订阅通道
        _redis.unsubscribe(user.getId());

        // 更新用户状态信息
        if (user.getId()!=-1)
        {
            user.setState("offline");
            _usermodel.updateState(user);
        }
       
    }

}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int toid=js["toid"];
    {   // 基本通信是建立在同一台服务器上进行通信；
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it!=_userConnMap.end())
        {
            // toid 在线 ，则转发消息，服务器主动推送消息给 toid 用户
            it->second->send(js.dump());
            return;
        }
    }
    // 表示toid 可能离线，则需要存储离线消息；可能属于其他服务器上的用户
    // 先查询是否其他服务器在线
    User user=_usermodel.query(toid);
    if (user.getState()=="online")
    {
        _redis.publish(toid, js.dump()); //通过 redis消息队列进行转发消息
        return;
    }

    // 否则 toid 离线，则需要存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
    
}
// 添加好友业务 ； msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"];
    int friendid = js["friendid"];
    _friendModel.insert(userid,friendid);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"];
    string name = js["groupname"];
    string desc = js["groupdesc"];
    // 存储 新创建的群组信息
    Group group(-1,name,desc);
    if (_groupModel.createGroup(group))
    {
        // 存储群组 创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}
// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"];
    int groupid = js["groupid"];
    _groupModel.addGroup(userid,groupid,"normal");

}
//群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"];
    int groupid = js["groupid"];
    vector<int> useridVec = _groupModel.queryGroupUsers(userid,groupid);

    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if (it!=_userConnMap.end())
        {
            // 转发消息  : 用户在线状态
            it->second->send(js.dump());// 等价于 conn->send()
        }
        else
        {
            // 表示toid 可能离线，则需要存储离线消息；可能属于其他服务器上的用户
            // 先查询是否其他服务器在线
            User user = _usermodel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump()); // 通过 redis消息队列进行转发消息
                return;
            }else
            {
                // 否则 toid 离线，则需要存储离线消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }    
}
// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it!=_userConnMap.end())
    {
        it->second->send(msg);
        return;
    }
    // 否则存储用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}
