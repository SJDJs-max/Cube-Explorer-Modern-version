#include "../src/app/cube_canvas.hpp"
#include <QApplication>
#include <QGridLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QMouseEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QWidget>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool condition,const char* message) {++checks;if(!condition)throw std::runtime_error(message);}
QPointF middle(const QPolygonF& p) {return (p[0]+p[1]+p[2]+p[3])/4;}
QPointF planePoint(const QPolygonF& p,QPointF position) {
    QTransform transform;require(QTransform::squareToQuad(p,transform),"Face plane must have a valid projective mapping");
    return transform.map(position);
}
void mouse(QWidget& widget,QEvent::Type type,QPointF pos,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers modifiers) {
    QMouseEvent event(type,pos,widget.mapToGlobal(pos),button,buttons,modifiers);QApplication::sendEvent(&widget,&event);
}
void orbit(CubeCanvas& canvas,QPointF delta) {
    const auto start=QPointF(canvas.rect().center());
    mouse(canvas,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    mouse(canvas,QEvent::MouseMove,start+delta,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    mouse(canvas,QEvent::MouseButtonRelease,start+delta,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
}
void verifyRenderedColor(const QImage& image,QPointF point,QColor expected) {
    const auto actual=image.pixelColor(qRound(point.x()*image.devicePixelRatio()),qRound(point.y()*image.devicePixelRatio()));
    const double magnitude=expected.red()*expected.red()+expected.green()*expected.green()+expected.blue()*expected.blue();
    const double illumination=(actual.red()*expected.red()+actual.green()*expected.green()+actual.blue()*expected.blue())/magnitude;
    require(illumination>.75&&illumination<1.08,"Rendered sticker must retain its specified color under lighting");
    require(std::abs(actual.red()-expected.red()*illumination)<6&&std::abs(actual.green()-expected.green()*illumination)<6&&
            std::abs(actual.blue()-expected.blue()*illumination)<6,"Rendering must use the real physical facelet color");
}
void screenshots(const QString& directory,bool dark) {
    QWidget sheet;sheet.resize(1260,490);
    sheet.setStyleSheet(QString("QWidget { background: %1; color: %2; font-family: 'Calibri', 'Microsoft YaHei'; } QLabel { font-size: 16px; font-weight:600; }")
        .arg(dark?"#111b2a":"#f4f7fb",dark?"#e6eef7":"#24364b"));
    auto* layout=new QGridLayout(&sheet);layout->setContentsMargins(24,22,24,20);layout->setHorizontalSpacing(22);
    auto pattern=cube::pattern_from_cube(cube::apply(cube::CubeState{},cube::parse_moves("R U F2 r' U")),true);
    pattern.cp[2]=-1;pattern.co[2]=-1;pattern.co[4]=-1;pattern.cp[6]=-1;pattern.co[6]=1;
    const std::array<const char*,3> names={"原版展开 · Classic","2D · 正方形贴纸","3D · 等比例实体"};
    const std::array<CubeCanvas::View,3> views={CubeCanvas::View::Classic,CubeCanvas::View::Net2D,CubeCanvas::View::Cube3D};
    for(int col=0;col<3;++col) {
        auto* label=new QLabel(names[col]);label->setAlignment(Qt::AlignCenter);layout->addWidget(label,0,col);
        auto* canvas=new CubeCanvas;canvas->setPattern(pattern);canvas->setView(views[col]);canvas->setDarkMode(dark);layout->addWidget(canvas,1,col);
        auto* small=new CubeCanvas(true);small->setPattern(cube::pattern_from_cube(cube::CubeState{}));small->setDarkMode(dark);small->setFixedSize(142,105);layout->addWidget(small,2,col,Qt::AlignHCenter);
    }
    sheet.show();QApplication::processEvents();
    const auto file=directory+(dark?"/cube-canvas-v2-night.png":"/cube-canvas-v2-day.png");
    require(sheet.grab().save(file),"Screenshot save failed");
}
void screenshots3D(const QString& directory,bool dark) {
    QWidget sheet;sheet.resize(1260,470);
    sheet.setStyleSheet(QString("QWidget { background: %1; color: %2; font-family: 'Calibri', 'Microsoft YaHei'; } QLabel { font-size: 17px; }")
        .arg(dark?"#1a1d23":"#f1f2f4",dark?"#e8ebef":"#26313c"));
    auto* layout=new QGridLayout(&sheet);layout->setContentsMargins(20,20,20,20);layout->setHorizontalSpacing(16);
    const auto moved=cube::apply(cube::CubeState{},cube::parse_moves("r U F2"));
    const std::array<const char*,3> names={"默认：白顶 · 绿前","真实宽转后的状态：r U F2","拖动观察同一个状态"};
    std::array<CubeCanvas*,3> canvases{};
    for(int col=0;col<3;++col) {
        auto* title=new QLabel(names[col]);title->setAlignment(Qt::AlignCenter);layout->addWidget(title,0,col);
        auto* canvas=new CubeCanvas;canvas->setView(CubeCanvas::View::Cube3D);canvas->setDarkMode(dark);
        canvas->setPattern(cube::pattern_from_cube(col==0?cube::CubeState{}:moved));layout->addWidget(canvas,1,col);canvases[col]=canvas;
    }
    sheet.show();QApplication::processEvents();orbit(*canvases[2],QPointF(130,-20));QApplication::processEvents();
    require(sheet.grab().save(directory+(dark?"/cube-canvas-v031-3d-night.png":"/cube-canvas-v031-3d-day.png")),"3D screenshot save failed");
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    if(QGuiApplication::platformName()=="offscreen") {
        const auto fonts=qEnvironmentVariable("WINDIR","C:/Windows")+"/Fonts/";
        for(const auto* name:{"calibri.ttf","calibrib.ttf","msyh.ttc","msyhbd.ttc","segoeui.ttf"})QFontDatabase::addApplicationFont(fonts+name);
    }
    QFont font;
    font.setFamilies({"Calibri", "Microsoft YaHei"});font.setPixelSize(16);app.setFont(font);
    try {
        CubeCanvas canvas;canvas.resize(540,420);canvas.show();QApplication::processEvents();
        for(const auto size:{QSize(540,420),QSize(930,280),QSize(250,650)}) {
            canvas.resize(size);QApplication::processEvents();
            for(const auto mode:{CubeCanvas::View::Classic,CubeCanvas::View::Net2D}) {
                canvas.setView(mode);
                for(int index=0;index<54;++index) {
                    const auto p=canvas.stickerPolygon(index);require(p.size()==4,"All 54 facelets must be visible");
                    require(canvas.stickerAt(middle(p))==index,"Hit index must identify physical facelet");
                    require(QRectF(canvas.rect()).contains(p.boundingRect()),"Aspect-preserving layout must stay inside viewport");
                    if(mode==CubeCanvas::View::Net2D) {
                        const auto width=QLineF(p[0],p[1]).length(),height=QLineF(p[1],p[2]).length();
                        require(std::abs(width-height)<1e-8,"2D facelets must remain squares at all aspect ratios");
                    }
                }
            }
        }
        canvas.resize(540,420);canvas.setView(CubeCanvas::View::Cube3D);int visible=0;
        for(int index=0;index<54;++index) {
            const auto polygon=canvas.stickerPolygon(index);if(polygon.isEmpty())continue;
            ++visible;require(canvas.stickerAt(middle(polygon))==index,"3D picking must match rendering");
        }
        require(visible==27,"Default camera must show exactly three complete faces");
        // Undo each face's perspective projection. Every tile must recover an
        // exact unit square on the same 3-by-3 physical plane, including depth.
        for(const int face:{0,1,2}) {
            const auto first=canvas.stickerPolygon(face*9);
            const QPolygonF facePolygon={first[0],canvas.stickerPolygon(face*9+2)[1],canvas.stickerPolygon(face*9+8)[2],canvas.stickerPolygon(face*9+6)[3]};
            QTransform inverseProjection;
            require(QTransform::quadToQuad(facePolygon,QPolygonF{{0,0},{3,0},{3,3},{0,3}},inverseProjection),"3D face projection must be invertible");
            for(int tile=0;tile<9;++tile) {
                const auto p=canvas.stickerPolygon(face*9+tile);
                const std::array<QPointF,4> corners={QPointF(tile%3,tile/3),QPointF(tile%3+1,tile/3),QPointF(tile%3+1,tile/3+1),QPointF(tile%3,tile/3+1)};
                for(int corner=0;corner<4;++corner)
                    require(QLineF(inverseProjection.map(p[corner]),corners[corner]).length()<1e-7,"All perspective stickers must be equal physical squares");
            }
        }
        require(QLineF(canvas.stickerPolygon(8)[2],canvas.stickerPolygon(9)[0]).length()<1e-8&&
                QLineF(canvas.stickerPolygon(8)[2],canvas.stickerPolygon(20)[1]).length()<1e-8,"Three faces must share one projected physical corner");
        QPointF verticalVanishingPoint;
        const auto leftVertical=QLineF(canvas.stickerPolygon(18)[0],canvas.stickerPolygon(24)[3]);
        const auto rightVertical=QLineF(canvas.stickerPolygon(20)[1],canvas.stickerPolygon(26)[2]);
        require(leftVertical.intersects(rightVertical,&verticalVanishingPoint)!=QLineF::NoIntersection,"Camera must use true perspective");
        require(std::abs(verticalVanishingPoint.x()-QRectF(canvas.rect()).center().x())<1e-6,"Upright camera must have no roll");
        const auto referenceVertex=(canvas.stickerPolygon(20)[1]-QRectF(canvas.rect()).center())/
            std::min(canvas.width()-28.0,canvas.height()-28.0);
        for(const auto size:{QSize(375,293),QSize(500,390),QSize(1000,780),QSize(930,280),QSize(250,650)}) {
            canvas.resize(size);QApplication::processEvents();
            const auto vertex=(canvas.stickerPolygon(20)[1]-QRectF(canvas.rect()).center())/
                std::min(canvas.width()-28.0,canvas.height()-28.0);
            require(QLineF(vertex,referenceVertex).length()<1e-8,"Resizing must scale all three axes uniformly without stretching faces");
            for(int index=0;index<54;++index) {
                const auto polygon=canvas.stickerPolygon(index);if(polygon.isEmpty())continue;
                require(QRectF(canvas.rect()).contains(polygon.boundingRect()),"Perspective cube must fit narrow and wide windows");
                require(canvas.stickerAt(middle(polygon))==index,"Resizing must preserve physical sticker picking");
            }
        }
        canvas.resize(540,420);QApplication::processEvents();
        int seen=-1,calls=0;Qt::MouseButton seenButton=Qt::NoButton;Qt::KeyboardModifiers seenModifiers;
        canvas.onSticker=[&](int index,Qt::MouseButton button,Qt::KeyboardModifiers modifiers){seen=index;seenButton=button;seenModifiers=modifiers;++calls;};
        canvas.setView(CubeCanvas::View::Net2D);
        auto p=middle(canvas.stickerPolygon(20));
        mouse(canvas,QEvent::MouseButtonPress,p,Qt::RightButton,Qt::RightButton,Qt::ControlModifier|Qt::ShiftModifier);
        require(calls==1&&seen==20&&seenButton==Qt::RightButton&&seenModifiers==(Qt::ControlModifier|Qt::ShiftModifier),"Editor must receive full original mouse modifiers");
        mouse(canvas,QEvent::MouseButtonRelease,p,Qt::RightButton,Qt::NoButton,Qt::ControlModifier|Qt::ShiftModifier);
        require(calls==1,"2D release must not apply a second edit");
        canvas.setView(CubeCanvas::View::Cube3D);p=middle(canvas.stickerPolygon(22));
        // Dragging right around the Y axis must rotate the front face right (same direction as mouse).
        canvas.resetCamera();
        const auto frontBefore = middle(canvas.stickerPolygon(22));
        mouse(canvas,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        mouse(canvas,QEvent::MouseMove,p+QPointF(50,0),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        mouse(canvas,QEvent::MouseButtonRelease,p+QPointF(50,0),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        const auto frontAfterRight = middle(canvas.stickerPolygon(22));
        require(frontAfterRight.x()>frontBefore.x(),"Dragging right in 3D must rotate the front face right");
        require(calls==1,"Dragging 3D camera must never edit cube state");
        canvas.resetCamera();p=middle(canvas.stickerPolygon(22));
        mouse(canvas,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,Qt::ControlModifier);
        mouse(canvas,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton,Qt::ControlModifier);
        require(calls==2&&seen==22&&seenModifiers==Qt::ControlModifier,"3D Ctrl click must preserve marker input");
        for(const auto symbol:cube::move_names)for(int power=1;power<=3;++power) {
            const auto state=cube::apply(cube::CubeState{},cube::Move{symbol,static_cast<std::uint8_t>(power)});
            canvas.setPattern(cube::pattern_from_cube(state,true));
            require(!canvas.grab().isNull(),"Every wide/slice/rotation state must paint");
        }
        const std::array<QColor,6> requested={QColor(255,255,255),QColor(238,0,0),QColor(0,216,0),
            QColor(254,254,0),QColor(255,161,0),QColor(0,0,242)};
        for(int face=0;face<6;++face) require(canvas.colors()[face]==requested[face],"Requested default RGB palette");
        // A camera orbit and a logical cube frame are independent. Across all
        // 24 frames and views above/below the cube, pick and inspect the actual
        // physical sticker, rather than rotating its labels to fake the view.
        std::array<bool,54> seenPhysical{};
        const std::array<QPointF,5> cameras={QPointF(0,0),QPointF(180,0),QPointF(360,0),QPointF(540,0),QPointF(0,-140)};
        const auto scramble=cube::apply(cube::CubeState{},cube::parse_moves("R U F2 r' U"));
        canvas.resize(540,420);canvas.setView(CubeCanvas::View::Cube3D);
        for(int frame=0;frame<24;++frame) {
            auto state=scramble;state.frame=static_cast<std::uint8_t>(frame);
            const auto expected=cube::to_facelets(state);const auto untouched=state;
            canvas.setPattern(cube::pattern_from_cube(state,true));
            for(const auto camera:cameras) {
                canvas.resetCamera();const int callsBefore=calls;
                if(!camera.isNull())orbit(canvas,camera);
                require(calls==callsBefore,"Camera orbit must never dispatch a sticker edit");
                const auto image=canvas.grab().toImage();
                for(int index=0;index<54;++index) {
                    const auto polygon=canvas.stickerPolygon(index);if(polygon.isEmpty())continue;
                    seenPhysical[index]=true;
                    const auto center=planePoint(polygon,QPointF(.5,.5));
                    require(canvas.stickerAt(center)==index,"Picking after a frame/camera change must keep physical URFDLB indices");
                    require(QRectF(canvas.rect()).contains(polygon.boundingRect()),"Orbit must preserve viewport bounds");
                    verifyRenderedColor(image,planePoint(polygon,QPointF(.22,.22)),requested[expected.colors[index]]);
                    mouse(canvas,QEvent::MouseButtonPress,center,Qt::RightButton,Qt::RightButton,Qt::ControlModifier|Qt::ShiftModifier);
                    mouse(canvas,QEvent::MouseButtonRelease,center,Qt::RightButton,Qt::NoButton,Qt::ControlModifier|Qt::ShiftModifier);
                    require(seen==index&&seenButton==Qt::RightButton&&seenModifiers==(Qt::ControlModifier|Qt::ShiftModifier),"Frame/orbit must preserve original right/Ctrl/Shift editing");
                }
                require(state==untouched,"Painting and camera orbit must not change cubies, frame, or center orientation");
            }
        }
        for(bool seenFacelet:seenPhysical)require(seenFacelet,"Camera coverage must inspect all 54 physical stickers");
        canvas.resetCamera();const auto beforeZoom=canvas.stickerPolygon(22);const int callsBeforeZoom=calls;
        const auto zoomOrigin=QPointF(canvas.rect().center());
        QWheelEvent zoomEvent(zoomOrigin,canvas.mapToGlobal(zoomOrigin),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&canvas,&zoomEvent);
        const auto afterZoom=canvas.stickerPolygon(22);
        require(afterZoom.boundingRect().width()>beforeZoom.boundingRect().width()&&canvas.stickerAt(middle(afterZoom))==22,"Zoom must scale the camera and retain picking");
        require(calls==callsBeforeZoom,"Camera zoom must not edit the cube");
        canvas.resetCamera();require(canvas.stickerPolygon(22)==beforeZoom,"Reset must restore the default camera exactly");
        auto markers=cube::format_facelets(cube::solved_facelets());
        markers[18]='u';markers[19]='@';markers[20]='r';
        std::array<std::int8_t,6> noCenters;noCenters.fill(-1);
        canvas.resize(900,700);canvas.setView(CubeCanvas::View::Net2D);
        canvas.setFacelets(markers,noCenters);canvas.setEditable(false);
        for(bool dark:{false,true}) {
            canvas.setDarkMode(dark);const auto rendered=canvas.grab().toImage();
            std::array<int,3> colored{},black{};
            for(int tile=0;tile<3;++tile) {
                const auto bounds=canvas.stickerPolygon(18+tile).boundingRect();
                const auto sample=bounds.adjusted(bounds.width()*.15,bounds.height()*.15,-bounds.width()*.15,-bounds.height()*.15).toRect();
                const auto expected=requested[tile==2?1:0];
                for(int y=sample.top();y<=sample.bottom();++y)for(int x=sample.left();x<=sample.right();++x) {
                    const auto pixel=rendered.pixelColor(qRound(x*rendered.devicePixelRatio()),qRound(y*rendered.devicePixelRatio()));
                    if(pixel.red()<8&&pixel.green()<8&&pixel.blue()<8)++black[tile];
                    if(std::abs(pixel.red()-expected.red())<30&&std::abs(pixel.green()-expected.green())<30&&std::abs(pixel.blue()-expected.blue())<30)++colored[tile];
                }
                require(black[tile]>sample.width()*sample.height()*.35,"Special marker has an opaque black background");
                require(colored[tile]>sample.width()*sample.height()*.07,"Special marker has clearly visible target-color lines");
            }
            require(colored[0]>colored[1]*1.4,"Shift crosshatch is visibly distinct from Ctrl single diagonal");
        }
        if(argc>1)for(bool dark:{false,true}) {screenshots(QString::fromLocal8Bit(argv[1]),dark);screenshots3D(QString::fromLocal8Bit(argv[1]),dark);}
        std::cout<<"PASS CubeCanvas: "<<checks<<" checks; square 2D, classic topology, upright perspective cubies, all 24 frames, physical picking/colors, Ctrl/Shift preservation, camera isolation, all 54 actions.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
