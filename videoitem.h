#pragma once

#include <QQuickPaintedItem>
#include <QImage>
#include <QMutex>

class VideoItem : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit VideoItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

public slots:
    void setFrame(const QImage &image);
private:
    QImage m_image;
    QMutex m_mutex;
};