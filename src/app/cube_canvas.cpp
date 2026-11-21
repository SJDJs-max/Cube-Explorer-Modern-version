#include "cube_canvas.hpp"

#include <QApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QTransform>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <numbers>
#include <stdexcept>

namespace {
struct Vec3 {
    double x, y, z;
    Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec3 operator*(double n) const { return {x*n,y*n,z*n}; }
};
double dot(Vec3 a,Vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
constexpr std::array<Vec3, 6> normal={{{0,1,0},{1,0,0},{0,0,1},{0,-1,0},{-1,0,0},{0,0,-1}}};
constexpr std::array<Vec3, 6> right={{{1,0,0},{0,0,-1},{1,0,0},{1,0,0},{0,0,1},{-1,0,0}}};
constexpr std::array<Vec3, 6> up={{{0,0,-1},{0,1,0},{0,1,0},{0,0,1},{0,1,0},{0,1,0}}};

QPolygonF quad(QPointF origin, QPointF horizontal, QPointF vertical) {
    return {origin, origin+horizontal, origin+horizontal+vertical, origin+vertical};
}
QPointF interpolate(const QPolygonF& polygon, double x, double y) {
    QTransform projection;
    if(QTransform::squareToQuad(polygon,projection))return projection.map(QPointF(x,y));
    return polygon[0]+(polygon[1]-polygon[0])*x+(polygon[3]-polygon[0])*y;
}
QPolygonF inset(const QPolygonF& polygon, double fraction) {
    return {interpolate(polygon,fraction,fraction),interpolate(polygon,1-fraction,fraction),
            interpolate(polygon,1-fraction,1-fraction),interpolate(polygon,fraction,1-fraction)};
}
QPainterPath rounded(const QPolygonF& polygon, double fraction) {
    QPainterPath path;
    path.addRoundedRect(QRectF(0,0,1,1),fraction,fraction);
    QTransform projection;
    if(QTransform::squareToQuad(polygon,projection))return projection.map(path);
    path.clear();path.addPolygon(polygon);path.closeSubpath();return path;
}
QColor lit(QColor color,double factor) {
    return QColor::fromRgbF(std::clamp(color.redF()*factor,0.0,1.0),
        std::clamp(color.greenF()*factor,0.0,1.0),std::clamp(color.blueF()*factor,0.0,1.0));
}
QColor contrasting(const QColor& color) {
    return color.redF()*.2126+color.greenF()*.7152+color.blueF()*.0722>.47 ? QColor("#142031") : QColor("#f6f8fc");
}
}

CubeCanvas::CubeCanvas(bool compact,QWidget* parent)
    :QWidget(parent),stickers_(cube::format_facelets(cube::solved_facelets())),
      colors_({QColor(255,255,255),QColor(238,0,0),QColor(0,216,0),QColor(254,254,0),
               QColor(255,161,0),QColor(0,0,242),QColor(128,128,128)}),compact_(compact),editable_(!compact) {
    logicalCenters_.fill(-1);physicalCenters_.fill(-1);
    setMouseTracking(true);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent,false);
    setMinimumSize(compact?96:220,compact?72:180);
}
QSize CubeCanvas::sizeHint() const { return compact_?QSize(150,112):QSize(430,330); }
QSize CubeCanvas::minimumSizeHint() const { return compact_?QSize(96,72):QSize(220,180); }

void CubeCanvas::setPattern(const cube::PatternState& pattern) {
    setFacelets(cube::format_pattern_facelets(pattern),pattern.center_ori);
}
void CubeCanvas::setFacelets(std::string facelets,const std::array<std::int8_t,6>& centers) {
    if(facelets.size()!=54)throw std::invalid_argument("CubeCanvas requires 54 physical facelets.");
    stickers_=std::move(facelets);logicalCenters_=centers;refreshCenterDirections();update();
}
void CubeCanvas::setView(View view) { if(view_!=view){view_=view;hovered_=-1;invalidateGeometry();} }
void CubeCanvas::setDarkMode(bool dark) {dark_=dark;update();}
void CubeCanvas::setColors(const std::array<QColor,7>& colors) {colors_=colors;update();}
void CubeCanvas::setColors(const std::array<QColor,6>& colors) {std::copy(colors.begin(),colors.end(),colors_.begin());update();}
void CubeCanvas::setEditable(bool editable) {editable_=editable;if(!editable)hovered_=-1;setCursor(Qt::ArrowCursor);update();}
void CubeCanvas::setShowFaceLabels(bool show) {labels_=show;update();}
void CubeCanvas::setDrawingMargin(double pixels) {drawingMargin_=pixels;invalidateGeometry();}
void CubeCanvas::setCamera(double yaw, double pitch, double zoom) {
    yaw_=yaw;pitch_=std::clamp(pitch,-85.0,85.0);zoom_=std::clamp(zoom,.65,1.15);invalidateGeometry();
}
void CubeCanvas::resetCamera() {
    yaw_=30;pitch_=35;zoom_=1;invalidateGeometry();
    if(onCameraChanged)onCameraChanged(yaw_,pitch_,zoom_);
}
void CubeCanvas::invalidateGeometry() {geometryDirty_=true;update();}

void CubeCanvas::refreshCenterDirections() {
    physicalCenters_.fill(-1);
    // The editor can temporarily contain impossible cubies. Decode only the
    // centers from a synthetic solved cube, so partial paint edits still render.
    try {
        auto centers=cube::solved_facelets();
        for(int face=0;face<6;++face) {
            const auto color=cube::face_names.find(static_cast<char>(std::toupper(static_cast<unsigned char>(stickers_[face*9+4]))));
            if(color==std::string_view::npos)return;
            for(int tile=0;tile<9;++tile)centers.colors[face*9+tile]=static_cast<std::int8_t>(color);
        }
        auto oriented=cube::from_facelets(centers);
        for(int face=0;face<6;++face)oriented.center_ori[face]=static_cast<std::uint8_t>(std::max<int>(0,logicalCenters_[face]));
        const auto displayed=cube::to_facelets(oriented);
        for(int face=0;face<6;++face) {
            const auto logical=displayed.colors[face*9+4];
            if(logicalCenters_[logical]>=0)physicalCenters_[face]=static_cast<std::int8_t>(displayed.center_ori[face]);
        }
    }catch(const std::exception&) { /* Incomplete center input has no arrows. */ }
}

void CubeCanvas::ensureGeometry() const {
    if(!geometryDirty_)return;
    geometryDirty_=false;tiles_.clear();faces_.clear();
    const double margin=drawingMargin_<0?(compact_?3:14):drawingMargin_;
    const QRectF viewport=QRectF(rect()).adjusted(margin,margin,-margin,-margin);
    if(viewport.width()<=0||viewport.height()<=0)return;
    if(view_==View::Cube3D) {
        const double yaw=yaw_*std::numbers::pi/180.0,pitch=pitch_*std::numbers::pi/180.0;
        const auto rotate=[&](Vec3 point) {
            const double x=std::cos(yaw)*point.x-std::sin(yaw)*point.z;
            const double z=std::sin(yaw)*point.x+std::cos(yaw)*point.z;
            return Vec3{x,std::cos(pitch)*point.y-std::sin(pitch)*z,std::sin(pitch)*point.y+std::cos(pitch)*z};
        };
        // Match the reference TwistyPlayer orbit: 30/35 degrees, distance 6
        // for a unit cube, and a 20-degree perspective lens. Our cube is three
        // units wide, hence distance 18. All faces share this single camera;
        // changing the camera never relabels a facelet or changes cube state.
        constexpr double cameraDistance=18.0;
        const double focalLength=std::min(viewport.width(),viewport.height())/
            (2.0*std::tan(10.0*std::numbers::pi/180.0))*zoom_;
        const QPointF origin=viewport.center();
        const auto project=[&](Vec3 point){const auto p=rotate(point);return origin+QPointF(p.x,-p.y)*(focalLength/(cameraDistance-p.z));};
        constexpr Vec3 lightDirection={-.408248290463863,.816496580927726,.408248290463863};
        const auto illumination=[&](Vec3 n){return std::max(0.0,dot(rotate(n),lightDirection));};
        const auto addFacet=[&](std::initializer_list<Vec3> vertices,Vec3 n) {
            Vec3 center={0,0,0};
            for(const auto p:vertices)center=center+p;
            center=center*(1.0/vertices.size());
            if(rotate(n).z*cameraDistance-dot(n,center)<=1e-7)return;
            QPolygonF polygon;
            for(const auto p:vertices)polygon<<project(p);
            faces_.push_back({polygon,rotate(center).z,.30+.70*illumination(n)});
        };
        // Twenty-six separate, uniformly sized cubies with actual beveled
        // edges. Their facets are projected in 3D before depth sorting; the
        // small gaps and bevels therefore follow the camera naturally.
        constexpr double half=.493,flat=.455;
        constexpr std::array<Vec3,3> axes={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
        for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z) {
            if(x==0&&y==0&&z==0)continue;
            const Vec3 center={double(x),double(y),double(z)};
            for(int face=0;face<6;++face) {
                const auto p=center+normal[face]*half;
                const auto h=right[face]*flat,v=up[face]*flat;
                addFacet({p+h+v,p+h+v*(-1),p+h*(-1)+v*(-1),p+h*(-1)+v},normal[face]);
            }
            for(int a=0;a<3;++a)for(int b=a+1;b<3;++b)for(int sa:{-1,1})for(int sb:{-1,1}) {
                const auto sideA=axes[a]*sa,sideB=axes[b]*sb,along=axes[3-a-b]*flat;
                const auto p=center+sideA*half+sideB*flat,q=center+sideA*flat+sideB*half;
                addFacet({p+along,p+along*(-1),q+along*(-1),q+along},(sideA+sideB)*(1/std::sqrt(2.0)));
            }
            for(int sx:{-1,1})for(int sy:{-1,1})for(int sz:{-1,1})
                addFacet({center+Vec3{sx*half,sy*flat,sz*flat},center+Vec3{sx*flat,sy*half,sz*flat},
                          center+Vec3{sx*flat,sy*flat,sz*half}},Vec3{double(sx),double(sy),double(sz)}*(1/std::sqrt(3.0)));
        }
        for(int face=0;face<6;++face) {
            const auto n=rotate(normal[face]);
            if(n.z*cameraDistance<=1.5+1e-7)continue;
            const auto point=[&](double x,double y){return normal[face]*1.5+right[face]*x+up[face]*y;};
            for(int row=0;row<3;++row)for(int col=0;col<3;++col) {
                const double left=col-1.5,top=1.5-row;
                const QPolygonF polygon={project(point(left,top)),project(point(left+1,top)),project(point(left+1,top-1)),project(point(left,top-1))};
                const double light=.84+.16*illumination(normal[face]);
                tiles_.push_back({face*9+row*3+col,polygon,rotate(point(col-1,1-row)).z,light});
            }
        }
        std::sort(faces_.begin(),faces_.end(),[](const auto& a,const auto& b){return a.depth<b.depth;});
        std::sort(tiles_.begin(),tiles_.end(),[](const auto& a,const auto& b){return a.depth<b.depth;});
    } else {
        const bool classic=view_==View::Classic;
        const double faceGap=classic?0.0:.20;
        const double logicalWidth=classic?11.0:12.0+3*faceGap;
        const double logicalHeight=classic?8.0:9.0+2*faceGap;
        const double scale=std::min(viewport.width()/logicalWidth,viewport.height()/logicalHeight);
        const QPointF origin=viewport.center()-QPointF(logicalWidth,logicalHeight)*(scale/2);
        const std::array<QPoint,6> net={QPoint(1,0),QPoint(2,1),QPoint(1,1),QPoint(1,2),QPoint(0,1),QPoint(3,1)};
        for(int face=0;face<6;++face) {
            QPointF start,horizontal(1,0),vertical(0,1);
            if(classic) {
                switch(face) {
                case 0:start={5,0};vertical={-2.0/3,2.0/3};break;
                case 1:start={6,2};horizontal={2.0/3,-2.0/3};break;
                case 2:start={3,2};break;
                case 3:start={3,5};break;
                case 4:start={0,2};break;
                case 5:start={8,0};break;
                }
            } else start=QPointF(net[face])*(3+faceGap);
            const auto physical=[&](QPointF p){return origin+p*scale;};
            faces_.push_back({quad(physical(start),horizontal*(3*scale),vertical*(3*scale)),0});
            for(int row=0;row<3;++row)for(int col=0;col<3;++col)
                tiles_.push_back({face*9+row*3+col,quad(physical(start+horizontal*col+vertical*row),horizontal*scale,vertical*scale),0,1});
        }
    }
}

int CubeCanvas::stickerAt(QPointF point) const {
    ensureGeometry();
    for(auto it=tiles_.rbegin();it!=tiles_.rend();++it)
        if(it->polygon.containsPoint(point,Qt::OddEvenFill))return it->index;
    return -1;
}
QPolygonF CubeCanvas::stickerPolygon(int index) const {
    ensureGeometry();
    for(const auto& tile:tiles_)if(tile.index==index)return tile.polygon;
    return {};
}

void CubeCanvas::paintEvent(QPaintEvent*) {
    ensureGeometry();QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
    const QColor plastic=dark_?QColor("#080c13"):QColor("#202a35");
    const bool classic=view_==View::Classic;
    const bool threeD=view_==View::Cube3D;
    if(threeD&&!faces_.empty()) {
        QRectF bounds;
        for(const auto& face:faces_)bounds|=face.polygon.boundingRect();
        painter.save();
        painter.translate(bounds.center().x(),bounds.bottom()+bounds.height()*.025);
        painter.scale(bounds.width()*.48,bounds.height()*.055);
        QRadialGradient shadow(QPointF(0,0),1);
        shadow.setColorAt(0,QColor(0,0,0,dark_?80:38));shadow.setColorAt(1,QColor(0,0,0,0));
        painter.setPen(Qt::NoPen);painter.setBrush(shadow);painter.drawEllipse(QRectF(-1,-1,2,2));
        painter.restore();
    }
    for(const auto& face:faces_) {
        if(threeD) {
            const auto shade=lit(QColor("#3c4452"),face.light);
            painter.setPen(QPen(shade,.35));painter.setBrush(shade);painter.drawPolygon(face.polygon);
            continue;
        }
        painter.setPen(QPen(dark_?QColor("#364558"):QColor("#223040"),compact_?.6:1.0));
        painter.setBrush(plastic);painter.drawPath(rounded(face.polygon,classic?(compact_?0:.012):.03));
    }
    for(const auto& tile:tiles_) {
        const char value=stickers_[tile.index];
        const auto colorIndex=cube::face_names.find(static_cast<char>(std::toupper(static_cast<unsigned char>(value))));
        QColor color=colorIndex==std::string_view::npos?colors_[6]:colors_[colorIndex];
        if(value=='@')color=colors_[0];
        const bool ignore=std::islower(static_cast<unsigned char>(value));
        const bool marked=ignore||value=='@';
        if(threeD&&!marked)color=lit(color,tile.light);
        // The reference's sticker width is .85 of each physical cubie.
        const double margin=classic?(compact_?.022:.034):(threeD?.075:.064);
        const auto facelet=inset(tile.polygon,margin);
        const auto shape=rounded(facelet,classic?(compact_?.015:.05):.07);
        if(threeD&&!marked) {
            QLinearGradient finish(facelet.boundingRect().topLeft(),facelet.boundingRect().bottomRight());
            finish.setColorAt(0,lit(color,1.025));finish.setColorAt(1,lit(color,.975));
            painter.setBrush(finish);painter.setPen(QPen(lit(color,.72),compact_?.35:.65));
        }else {painter.setBrush(color);painter.setPen(QPen(color.darker(122),compact_?.35:.65));}
        painter.drawPath(shape);
        if(marked) {
            painter.save();painter.setClipPath(shape);
            // FaceCube.pas:372-373: identity-only uses target-color diagonal
            // crosshatching; orientation-only always uses U color, even when
            // another brush is selected. Marker semantics remain lowercase/@.
            painter.fillPath(shape,Qt::black);
            const double edge=std::min(QLineF(facelet[0],facelet[1]).length(),
                                       QLineF(facelet[0],facelet[3]).length());
            painter.setPen(QPen(color,std::max(.8,edge*.035),Qt::SolidLine,Qt::FlatCap));
            for(int stripe=-5;stripe<=5;++stripe) {
                const double offset=stripe*.2;
                painter.drawLine(interpolate(facelet,offset,0),interpolate(facelet,offset+1,1));
                if(ignore)
                    painter.drawLine(interpolate(facelet,offset,1),interpolate(facelet,offset+1,0));
            }
            painter.restore();
        }
        if(tile.index==hovered_&&editable_) {
            painter.setBrush(QColor(255,255,255,26));painter.setPen(QPen(dark_?QColor("#a6d6ff"):QColor("#235b90"),1.8));painter.drawPath(shape);
        }
        if(tile.index%9==4) {
            const int face=tile.index/9,orientation=physicalCenters_[face];
            if(orientation>=0) {
                // Draw in the actual tile plane, not its axis-aligned bounds.
                // Thus the arrow follows both the physical center and camera.
                constexpr std::array<QPointF,4> directions={QPointF(0,-1),QPointF(1,0),QPointF(0,1),QPointF(-1,0)};
                const auto direction=directions[orientation];
                const auto tangent=QPointF(-direction.y(),direction.x());
                const QPointF center(.5,.5),tip=center+direction*.29,tail=center-direction*.22;
                const QPointF wing=center+direction*.09;
                painter.setPen(QPen(contrasting(color),compact_?1.0:2.0,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                painter.drawLine(interpolate(facelet,tail.x(),tail.y()),interpolate(facelet,tip.x(),tip.y()));
                painter.drawLine(interpolate(facelet,tip.x(),tip.y()),interpolate(facelet,(wing+tangent*.13).x(),(wing+tangent*.13).y()));
                painter.drawLine(interpolate(facelet,tip.x(),tip.y()),interpolate(facelet,(wing-tangent*.13).x(),(wing-tangent*.13).y()));
            }else if(labels_&&!compact_) {
                painter.setPen(contrasting(color));QFont font=painter.font();font.setBold(true);
                font.setPixelSize(std::clamp(static_cast<int>(facelet.boundingRect().height()*.32),9,18));painter.setFont(font);
                painter.drawText(facelet.boundingRect(),Qt::AlignCenter,QString(cube::face_names[face]));
            }
        }
    }
}
void CubeCanvas::resizeEvent(QResizeEvent* event) {geometryDirty_=true;QWidget::resizeEvent(event);}
void CubeCanvas::dispatchClick(QPointF position,Qt::MouseButton button,Qt::KeyboardModifiers modifiers) {
    if(!editable_)return;
    const int index=stickerAt(position);
    if(index>=0) {if(onSticker)onSticker(index,button,modifiers);}
    else if(onBackground)onBackground(button,modifiers);
}
void CubeCanvas::mousePressEvent(QMouseEvent* event) {
    pressedButton_=event->button();pressedModifiers_=event->modifiers();pressPosition_=previousPosition_=event->position();dragging_=false;
    if(view_!=View::Cube3D)dispatchClick(event->position(),event->button(),event->modifiers());
    event->accept();
}
void CubeCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if(view_==View::Cube3D&&!dragging_&&pressedButton_==event->button())
        dispatchClick(event->position(),event->button(),pressedModifiers_);
    pressedButton_=Qt::NoButton;dragging_=false;setCursor(Qt::ArrowCursor);event->accept();
}
void CubeCanvas::mouseMoveEvent(QMouseEvent* event) {
    if(view_==View::Cube3D&&pressedButton_==Qt::LeftButton&&pressedModifiers_==Qt::NoModifier) {
        if(!dragging_&&(event->position()-pressPosition_).manhattanLength()>=QApplication::startDragDistance())dragging_=true;
        if(dragging_) {
            const auto delta=event->position()-previousPosition_;
            // Subtract delta.x so dragging right rotates the front face right
            // around the vertical Y axis, matching natural hand/cursor motion.
            yaw_-=delta.x()*.5;pitch_=std::clamp(pitch_+delta.y()*.5,-85.0,85.0);
            previousPosition_=event->position();hovered_=-1;setCursor(Qt::ClosedHandCursor);invalidateGeometry();
            if(onCameraChanged)onCameraChanged(yaw_,pitch_,zoom_);
            event->accept();return;
        }
    }
    previousPosition_=event->position();const int index=stickerAt(event->position());
    if(index!=hovered_) {hovered_=index;update();}
    setCursor(editable_&&index>=0?Qt::PointingHandCursor:(view_==View::Cube3D?Qt::OpenHandCursor:Qt::ArrowCursor));
}
void CubeCanvas::leaveEvent(QEvent* event) {hovered_=-1;update();QWidget::leaveEvent(event);}
void CubeCanvas::wheelEvent(QWheelEvent* event) {
    if(view_!=View::Cube3D) {event->ignore();return;}
    zoom_=std::clamp(zoom_*std::pow(1.001,event->angleDelta().y()),.65,1.15);invalidateGeometry();
    if(onCameraChanged)onCameraChanged(yaw_,pitch_,zoom_);
    event->accept();
}
