#include "VideoItem.h"

#include <QPainter>
#include <QMutexLocker>

VideoItem::VideoItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

void VideoItem::paint(QPainter *painter)
{
    QMutexLocker locker(&m_mutex);

    if (m_image.isNull())
    {
        painter->fillRect(boundingRect(), Qt::black);
        return;
    }

    painter->drawImage(boundingRect(), m_image);
}

void VideoItem::setFrame(const QImage &image)
{
    {
        QMutexLocker locker(&m_mutex);
        m_image = image.copy();
    }

    update();
}