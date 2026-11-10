#pragma once

#include "xcom/api/types.h"

#include <QObject>

#include <functional>

namespace xcom {

// 注册类 API 的通用撤销令牌。回调执行后对象自动销毁（deleteLater）。
class CallbackSubscription : public QObject, public ISubscription
{
public:
    explicit CallbackSubscription(std::function<void()> callback, QObject *parent = nullptr);

    void unsubscribe() override;

private:
    std::function<void()> m_callback;
    bool m_done = false;
};

} // namespace xcom
