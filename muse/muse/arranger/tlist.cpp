//=========================================================
//  MusE
//  Linux Music Editor
//    $Id: tlist.cpp,v 1.31.2.31 2009/12/15 03:39:58 terminator356 Exp $
//  (C) Copyright 1999 Werner Schweer (ws@seh.de)
//=========================================================

//#include "config.h"

#include <cmath>

#include <qpainter.h>
#include <qlineedit.h>
#include <qpopupmenu.h>
#include <qmessagebox.h>
#include <qscrollbar.h>
#include <qtimer.h>
#include <qfileinfo.h>

#include "globals.h"
#include "icons.h"
#include "scrollscale.h"
#include "tlist.h"
#include "xml.h"
#include "mididev.h"
#include "midiport.h"
#include "midiseq.h"
#include "comment.h"
#include "track.h"
#include "song.h"
#include "header.h"
#include "node.h"
#include "audio.h"
#include "instruments/minstrument.h"
#include "app.h"
#include "gconfig.h"
#include "event.h"
#include "midiedit/drummap.h"
#include "synth.h"

extern QPopupMenu* populateAddSynth(QWidget* parent, QObject* obj = 0, const char* slot = 0);

static const int MIN_TRACKHEIGHT = 20;
static const int WHEEL_DELTA = 120;

//---------------------------------------------------------
//   THeaderTip::maybeTip
//---------------------------------------------------------

void THeaderTip::maybeTip(const QPoint &pos)
      {
      Header* w  = (Header*)parentWidget();
      int section = w->sectionAt(pos.x());
      if (section == -1)
            return;
      QRect r(w->sectionPos(section), 0, w->sectionSize(section),
         w->height());
      QString p;
      switch (section) {
            case COL_RECORD:   p = QHeader::tr("Enable Recording"); break;
            case COL_MUTE:     p = QHeader::tr("Mute Indicator"); break;
            case COL_SOLO:     p = QHeader::tr("Solo Indicator"); break;
            case COL_CLASS:    p = QHeader::tr("Track Type"); break;
            case COL_NAME:     p = QHeader::tr("Track Name"); break;
            case COL_OCHANNEL: p = QHeader::tr("Midi output channel number or audio channels"); break;
            //case COL_OPORT:    p = QHeader::tr("Output Port"); break;
            case COL_OPORT:    p = QHeader::tr("Midi output port or synth midi port"); break;
            case COL_TIMELOCK: p = QHeader::tr("Time Lock"); break;
            default: return;
            }
      tip(r, p);
      }

//---------------------------------------------------------
//   TList
//---------------------------------------------------------

TList::TList(Header* hdr, QWidget* parent, const char* name)
   : QWidget(parent, name, WRepaintNoErase | WResizeNoErase)
      {
      ypos = 0;
      editMode = false;
      setFocusPolicy(QWidget::StrongFocus);
      setMouseTracking(true);
      header    = hdr;

      scroll    = 0;
      editTrack = 0;
      editor    = 0;
      mode      = NORMAL;

      setBackgroundMode(NoBackground);
      resizeFlag = false;

      connect(song, SIGNAL(songChanged(int)), SLOT(songChanged(int)));
      connect(muse, SIGNAL(configChanged()), SLOT(redraw()));
      }

//---------------------------------------------------------
//   songChanged
//---------------------------------------------------------

void TList::songChanged(int flags)
      {
      if (flags & (SC_MUTE | SC_SOLO | SC_RECFLAG | SC_TRACK_INSERTED
         | SC_TRACK_REMOVED | SC_TRACK_MODIFIED | SC_ROUTE | SC_CHANNELS | SC_MIDI_CHANNEL))
            redraw();
      if (flags & (SC_TRACK_INSERTED | SC_TRACK_REMOVED | SC_TRACK_MODIFIED))
            adjustScrollbar();
      }

//---------------------------------------------------------
//   drawCenteredPixmap
//    small helper function for "draw()" below
//---------------------------------------------------------

static void drawCenteredPixmap(QPainter& p, const QPixmap* pm, const QRect& r)
      {
      p.drawPixmap(r.x() + (r.width() - pm->width())/2, r.y() + (r.height() - pm->height())/2, *pm);
      }

//---------------------------------------------------------
//   paintEvent
//---------------------------------------------------------

void TList::paintEvent(QPaintEvent* ev)
      {
      if (!pmValid)
            paint(ev->rect());
      bitBlt(this, ev->rect().topLeft(), &pm, ev->rect(), CopyROP, true);
      }

//---------------------------------------------------------
//   redraw
//---------------------------------------------------------

void TList::redraw()
      {
      paint(QRect(0, 0, pm.width(), pm.height()));
      update();
      }

//---------------------------------------------------------
//   redraw
//---------------------------------------------------------

void TList::redraw(const QRect& r)
      {
      paint(r);
      update(r);
      }

//---------------------------------------------------------
//   paint
//---------------------------------------------------------

void TList::paint(const QRect& r)
      {
      if (!isVisible())
            return;
      QRect rect(r);
      if (!pmValid) {
            pmValid = true;
            rect = QRect(0, 0, pm.width(), pm.height());
            }
      QPainter p(&pm);

      if (bgPixmap.isNull())
            p.fillRect(rect, config.trackBg);
      else
            p.drawTiledPixmap(rect, bgPixmap, QPoint(rect.x(), ypos + rect.y()));
      p.setClipRegion(rect);

      int y  = rect.y();
      int w  = rect.width();
      int h  = rect.height();
      int x1 = rect.x();
      int x2 = rect.x() + w;

      //---------------------------------------------------
      //    Tracks
      //---------------------------------------------------

      TrackList* l = song->tracks();
      int idx = 0;
      int yy  = -ypos;
      for (iTrack i = l->begin(); i != l->end(); ++idx, yy += (*i)->height(), ++i) {
            Track* track = *i;
            Track::TrackType type = track->type();
            int trackHeight = track->height();
            if (yy >= (y + h))
                  break;
            if ((yy + trackHeight) < y)
                  continue;
            //
            // clear one row
            //
            QColor bg;
            if (track->selected()) {
                  bg = config.selectTrackBg;
                  p.setPen(config.selectTrackFg);
                  }
            else {
                  switch(type) {
                        case Track::MIDI:
                              bg = config.midiTrackBg;
                              break;
                        case Track::DRUM:
                              bg = config.drumTrackBg;
                              break;
                        case Track::WAVE:
                              bg = config.waveTrackBg;
                              break;
                        case Track::AUDIO_OUTPUT:
                              bg = config.outputTrackBg;
                              break;
                        case Track::AUDIO_INPUT:
                              bg = config.inputTrackBg;
                              break;
                        case Track::AUDIO_GROUP:
                              bg = config.groupTrackBg;
                              break;
                        case Track::AUDIO_AUX:
                              bg = config.auxTrackBg;
                              break;
                        case Track::AUDIO_SOFTSYNTH:
                              bg = config.synthTrackBg;
                              break;
                        }
                  p.setPen(palette().active().text());
                  }
            p.fillRect(x1, yy, w, trackHeight, bg);

            int x = 0;
            for (int index = 0; index < header->count(); ++index) {
                  int section = header->mapToSection(index);
                  int w   = header->sectionSize(section);
                  QRect r = p.xForm(QRect(x+2, yy, w-4, trackHeight));

                  switch (section) {
                        case COL_RECORD:
                              if (track->canRecord()) {
                                    drawCenteredPixmap(p,
                                       track->recordFlag() ? record_on_Icon : record_off_Icon, r);
                                    }
                              break;
                        case COL_CLASS:
                              {
                              const QPixmap* pm = 0;
                              switch(type) {
                                    case Track::MIDI:
                                          pm = addtrack_addmiditrackIcon;
                                          break;
                                    case Track::DRUM:
                                          pm = addtrack_drumtrackIcon;
                                          break;
                                    case Track::WAVE:
                                          pm = addtrack_wavetrackIcon;
                                          break;
                                    case Track::AUDIO_OUTPUT:
                                          pm = addtrack_audiooutputIcon;
                                          break;
                                    case Track::AUDIO_INPUT:
                                          pm = addtrack_audioinputIcon;
                                          break;
                                    case Track::AUDIO_GROUP:
                                          pm = addtrack_audiogroupIcon;
                                          break;
                                    case Track::AUDIO_AUX:
                                          pm = addtrack_auxsendIcon;
                                          break;
                                    case Track::AUDIO_SOFTSYNTH:
                                          //pm = waveIcon;
                                          pm = synthIcon;
                                          break;
                                    }
                              drawCenteredPixmap(p, pm, r);
                              }
                              break;
                        case COL_MUTE:
                              if (track->off())
                                    drawCenteredPixmap(p, offIcon, r);
                              else if (track->mute())
                                    drawCenteredPixmap(p, editmuteSIcon, r);
                              break;
                        case COL_SOLO:
                              if(track->solo() && track->internalSolo())
                                    drawCenteredPixmap(p, blacksqcheckIcon, r);
                              else      
                              if(track->internalSolo())
                                    drawCenteredPixmap(p, blacksquareIcon, r);
                              else
                              if (track->solo())
                                    drawCenteredPixmap(p, bluedotIcon, r);
                              break;
                        case COL_TIMELOCK:
                              if (track->isMidiTrack()
                                 && track->locked()) {
                                    drawCenteredPixmap(p, lockIcon, r);
                                    }
                              break;
                        case COL_NAME:
                              p.drawText(r, Qt::AlignVCenter|Qt::AlignLeft, track->name());
                              break;
                        case COL_OCHANNEL:
                              {
                              QString s;
                              int n;
                              if (track->isMidiTrack()) {
                                    n = ((MidiTrack*)track)->outChannel() + 1;
                                    }
                              else {
                                    // show number of ports
                                    n = ((WaveTrack*)track)->channels();
                                    }
                              s.setNum(n);
                              p.drawText(r, Qt::AlignVCenter|Qt::AlignHCenter, s);
                              }
                              break;
                        case COL_OPORT:
                              {
                              QString s;
                              if (track->isMidiTrack()) {
                                    int outport = ((MidiTrack*)track)->outPort();
                                    s.sprintf("%d:%s", outport+1, midiPorts[outport].portname().latin1());
                                    }
                              // Added by Tim. p3.3.9
                              
                              else
                              if(track->type() == Track::AUDIO_SOFTSYNTH)
                              {
                                MidiDevice* md = dynamic_cast<MidiDevice*>(track);  
                                if(md)
                                {
                                  int outport = md->midiPort();
                                  if((outport >= 0) && (outport < MIDI_PORTS))
                                    s.sprintf("%d:%s", outport+1, midiPorts[outport].portname().latin1());
                                  else
                                    s = tr("<none>");
                                }  
                              }  
                              
                              p.drawText(r, Qt::AlignVCenter|Qt::AlignLeft, s);
                              }
                              break;
                        default:
                              break;
                        }
                  x += header->sectionSize(section);
                  }
            p.setPen(gray);
            p.drawLine(x1, yy, x2, yy);
            }
      p.drawLine(x1, yy, x2, yy);

      if (mode == DRAG) {
            int yy = curY - dragYoff;
            p.setPen(green);
            p.drawLine(x1, yy, x2, yy);
            p.drawLine(x1, yy + dragHeight, x2, yy+dragHeight);
            }

      //---------------------------------------------------
      //    draw vertical lines
      //---------------------------------------------------

      int n = header->count();
      int xpos = 0;
      p.setPen(gray);
      for (int index = 0; index < n; index++) {
            int section = header->mapToSection(index);
            xpos += header->sectionSize(section);
            p.drawLine(xpos, 0, xpos, height());
            }
      }

//---------------------------------------------------------
//   returnPressed
//---------------------------------------------------------

void TList::returnPressed()
      {
      editor->hide();
      if (editor->text() != editTrack->name()) {
            TrackList* tl = song->tracks();
            for (iTrack i = tl->begin(); i != tl->end(); ++i) {
                  if ((*i)->name() == editor->text()) {
                        QMessageBox::critical(this,
                           tr("MusE: bad trackname"),
                           tr("please choose a unique track name"),
                           QMessageBox::Ok,
                           QMessageBox::NoButton,
                           QMessageBox::NoButton);
                        editTrack = 0;
                        setFocus();
                        return;
                        }
                  }
            //Track* track = editTrack->clone();
            Track* track = editTrack->clone(false);
            editTrack->setName(editor->text());
            audio->msgChangeTrack(track, editTrack);
            }
      editTrack = 0;
      editMode = false;
      setFocus();
      }

//---------------------------------------------------------
//   adjustScrollbar
//---------------------------------------------------------

void TList::adjustScrollbar()
      {
      int h = 0;
      TrackList* l = song->tracks();
      for (iTrack it = l->begin(); it != l->end(); ++it)
            h += (*it)->height();
      scroll->setMaxValue(h +30);
      redraw();
      }

//---------------------------------------------------------
//   y2Track
//---------------------------------------------------------

Track* TList::y2Track(int y) const
      {
      TrackList* l = song->tracks();
      int ty = 0;
      for (iTrack it = l->begin(); it != l->end(); ++it) {
            int h = (*it)->height();
            if (y >= ty && y < ty + h)
                  return *it;
            ty += h;
            }
      return 0;
      }

//---------------------------------------------------------
//   viewMouseDoubleClickEvent
//---------------------------------------------------------

void TList::mouseDoubleClickEvent(QMouseEvent* ev)
      {
      int x       = ev->x();
      int section = header->sectionAt(x);
      if (section == -1)
            return;

      Track* t = y2Track(ev->y() + ypos);

      if (t) {
            int colx = header->sectionPos(section);
            int colw = header->sectionSize(section);
            int coly = t->y() - ypos;
            int colh = t->height();

            if (section == COL_NAME) {
                  editTrack = t;
                  if (editor == 0) {
                        editor = new QLineEdit(this);
                        /*connect(editor, SIGNAL(returnPressed()),
                           SLOT(returnPressed()));*/
                        editor->setFrame(true);
                        }
                  editor->setText(editTrack->name());
                  editor->end(false);
                  editor->setGeometry(colx, coly, colw, colh);
                  editMode = true;
                  editor->show();
                  }
            else
                  mousePressEvent(ev);
            }
      }

//---------------------------------------------------------
//   portsPopupMenu
//---------------------------------------------------------

void TList::portsPopupMenu(Track* t, int x, int y)
      {
      switch(t->type()) {
            case Track::MIDI:
            case Track::DRUM:
            case Track::AUDIO_SOFTSYNTH: 
                  {
                  MidiTrack* track = (MidiTrack*)t;
                  
                  //QPopupMenu* p = midiPortsPopup(0);
                  MidiDevice* md = 0;
                  int port = -1; 
                  if(t->type() == Track::AUDIO_SOFTSYNTH) 
                  {
                    //MidiDevice* md = dynamic_cast<MidiDevice*>((SynthI*)t);
                    md = dynamic_cast<MidiDevice*>(t);
                    if(md)
                      port = md->midiPort(); 
                  }
                  else   
                    port = track->outPort();
                    
                  QPopupMenu* p = midiPortsPopup(0, port);
                  int n = p->exec(mapToGlobal(QPoint(x, y)), 0);
                  if (n != -1) {
                        // Changed by T356.
                        //track->setOutPort(n);
                        //audio->msgSetTrackOutPort(track, n);
                        
                        //song->update();
                        if (t->type() == Track::DRUM) {
                              bool change = QMessageBox::question(this, tr("Update drummap?"),
                                             tr("Do you want to use same port for all instruments in the drummap?"),
                                             tr("&Yes"), tr("&No"), QString::null, 0, 1);
                              audio->msgIdle(true);
                              if (!change) 
                              {
                                    // Delete all port controller events.
                                    //audio->msgChangeAllPortDrumCtrlEvents(false);
                                    song->changeAllPortDrumCtrlEvents(false);
                                    track->setOutPort(n);
                        
                                    for (int i=0; i<DRUM_MAPSIZE; i++) //Remap all drum instruments to this port
                                          drumMap[i].port = track->outPort();
                                    // Add all port controller events.
                                    //audio->msgChangeAllPortDrumCtrlEvents(true);
                                    song->changeAllPortDrumCtrlEvents(true);
                              }
                              else
                              {
                                //audio->msgSetTrackOutPort(track, n);
                                track->setOutPortAndUpdate(n);
                              }
                              audio->msgIdle(false);
                              song->update();
                        }
                        else
                        if (t->type() == Track::AUDIO_SOFTSYNTH) 
                        {
                          if(md != 0)
                          {
                            // Idling is already handled in msgSetMidiDevice.
                            //audio->msgIdle(true);
                            
                            // Compiler complains if simple cast from Track to SynthI...
                            midiSeq->msgSetMidiDevice(&midiPorts[n], (midiPorts[n].device() == md) ? 0 : md);
                            muse->changeConfig(true);     // save configuration file
                          
                            //audio->msgIdle(false);
                            song->update();
                          }
                        }
                        else
                        {
                          audio->msgIdle(true);
                          //audio->msgSetTrackOutPort(track, n);
                          track->setOutPortAndUpdate(n);
                          audio->msgIdle(false);
                          song->update();
                        }
                      }
                  delete p;
                  }
                  break;
                  
            case Track::WAVE:
            case Track::AUDIO_OUTPUT:
            case Track::AUDIO_INPUT:
            case Track::AUDIO_GROUP:
            case Track::AUDIO_AUX:    //TODO
                  break;
            }
      }

//---------------------------------------------------------
//   oportPropertyPopupMenu
//---------------------------------------------------------

void TList::oportPropertyPopupMenu(Track* t, int x, int y)
      {
      // Added by Tim. p3.3.9
      if(t->type() == Track::AUDIO_SOFTSYNTH)
      {
        SynthI* synth = (SynthI*)t;
  
        QPopupMenu* p = new QPopupMenu(this);
        p->setCheckable(true);
        p->insertItem(tr("Show Gui"), 0);
  
        p->setItemEnabled(0, synth->hasGui());
        p->setItemChecked(0, synth->guiVisible());
  
        int n = p->exec(mapToGlobal(QPoint(x, y)), 0);
        if (n == 0) {
              bool show = !synth->guiVisible();
              audio->msgShowInstrumentGui(synth, show);
              }
        delete p;
        return;
      }
      
      
      if (t->type() != Track::MIDI && t->type() != Track::DRUM)
            return;
      int oPort      = ((MidiTrack*)t)->outPort();
      MidiPort* port = &midiPorts[oPort];

      QPopupMenu* p = new QPopupMenu(this);
      p->setCheckable(true);
      p->insertItem(tr("Show Gui"), 0);

      p->setItemEnabled(0, port->hasGui());
      p->setItemChecked(0, port->guiVisible());

      int n = p->exec(mapToGlobal(QPoint(x, y)), 0);
      if (n == 0) {
            bool show = !port->guiVisible();
            audio->msgShowInstrumentGui(port->instrument(), show);
            }
      delete p;
      
      }

//---------------------------------------------------------
//   tracklistChanged
//---------------------------------------------------------

void TList::tracklistChanged()
      {
      redraw();
      }

//---------------------------------------------------------
//   keyPressEvent
//---------------------------------------------------------

void TList::keyPressEvent(QKeyEvent* e)
      {
      if (editMode) {
            // First time we get a keypress event when lineedit is open is on the return key:
            returnPressed();
            return;
            }
      emit keyPressExt(e); //redirect keypress events to main app
      e->ignore();
      /*
      int key = e->key();
      switch (key) {
            case Key_Up:
                  moveSelection(-1);
                  break;
            case Key_Down:
                  moveSelection(1);
                  break;
            default:

                  break;
            }
            */
      }

//---------------------------------------------------------
//   moveSelection
//---------------------------------------------------------

void TList::moveSelection(int n)
      {
      TrackList* tracks = song->tracks();

      // check for single selection
      int nselect = 0;
      for (iTrack t = tracks->begin(); t != tracks->end(); ++t)
            if ((*t)->selected())
                  ++nselect;
      if (nselect != 1)
            return;
      for (iTrack t = tracks->begin(); t != tracks->end(); ++t) {
            iTrack s = t;
            if ((*t)->selected()) {
                  if (n > 0) {
                        while (n--) {
                              ++t;
                              if (t == tracks->end()) {
                                    --t;
                                    break;
                                    }
                              }
                        }
                  else {
                        while (n++ != 0) {
                              if (t == tracks->begin())
                                    break;
                              --t;
                              }
                        }
                  (*s)->setSelected(false);
                  (*t)->setSelected(true);
                  if (editTrack && editTrack != *t)
                        returnPressed();
                  redraw();
                  break;
                  }
            }
      emit selectionChanged();
      }

//---------------------------------------------------------
//   mousePressEvent
//---------------------------------------------------------

void TList::mousePressEvent(QMouseEvent* ev)
      {
      int x       = ev->x();
      int y       = ev->y();
      int button  = ev->button();
      bool shift  = ev->state() & ShiftButton;

      Track* t    = y2Track(y + ypos);

      TrackColumn col = TrackColumn(header->sectionAt(x));
      if (t == 0) {
            if (button == QMouseEvent::RightButton) {
                  QPopupMenu* p = new QPopupMenu(this);
                  p->clear();
                  p->insertItem(*addtrack_addmiditrackIcon,
                     tr("Add Midi Track"), Track::MIDI, 0);
                  p->insertItem(*addtrack_drumtrackIcon,
                     tr("Add Drum Track"),Track::DRUM, 1);
                  p->insertItem(*addtrack_wavetrackIcon,
                     tr("Add Wave Track"), Track::WAVE, 2);
                  p->insertItem(*addtrack_audiooutputIcon,
                     tr("Add Output"), Track::AUDIO_OUTPUT, 3);
                  p->insertItem(*addtrack_audiogroupIcon,
                     tr("Add Group"), Track::AUDIO_GROUP, 4);
                  p->insertItem(*addtrack_audioinputIcon,
                     tr("Add Input"), Track::AUDIO_INPUT, 5);
                  p->insertItem(*addtrack_auxsendIcon,
                     tr("Add Aux Send"), Track::AUDIO_AUX, 6);
                  
                  // Create a sub-menu and fill it with found synth types. Make p the owner.
                  QPopupMenu* synp = populateAddSynth(p);
                  // Add the 'Add Synth' sub-menu to the menu.
                  p->insertItem(*synthIcon, tr("Add Synth"), synp, Track::AUDIO_SOFTSYNTH);
                  
                  // Show the menu
                  int n = p->exec(ev->globalPos(), 0);

                  // Valid click?
                  if((n >= 0) && ((Track::TrackType)n != Track::AUDIO_SOFTSYNTH))
                  {
                    // Synth sub-menu id?
                    if(n >= MENU_ADD_SYNTH_ID_BASE)
                    {
                      n -= MENU_ADD_SYNTH_ID_BASE;
                      //if(n < synthis.size())
                      //  t = song->createSynthI(synthis[n]->baseName());
                      //if((n - MENU_ADD_SYNTH_ID_BASE) < (int)synthis.size())
                      if(n < (int)synthis.size())
                      {
                        //t = song->createSynthI(synp->text(n));
                        //t = song->createSynthI(synthis[n]->name());                        
                        t = song->createSynthI(synthis[n]->baseName(), synthis[n]->name());
                        
                        if(t)
                        {
                          // Add instance last in midi device list.
                          for (int i = 0; i < MIDI_PORTS; ++i) 
                          {
                            MidiPort* port  = &midiPorts[i];
                            MidiDevice* dev = port->device();
                            if (dev==0) 
                            {
                              midiSeq->msgSetMidiDevice(port, (SynthI*)t);
                              muse->changeConfig(true);     // save configuration file
                              song->update();
                              break;
                            }
                          }  
                        }
                      }  
                    }  
                    // Normal track.
                    else
                      t = song->addTrack((Track::TrackType)n);
                    
                    if(t)
                    {
                      song->deselectTracks();
                      t->setSelected(true);
                      emit selectionChanged();
                      adjustScrollbar();
                    }  
                  }
                  
                  // Just delete p, and all its children will go too, right?
                  //delete synp;
                  delete p;
                 }
            return;
            }

      TrackList* tracks = song->tracks();
      dragYoff = y - (t->y() - ypos);
      startY   = y;

      if (resizeFlag) {
            mode = RESIZE;
            int y  = ev->y();
            int ty = -ypos;
            sTrack = 0;
            for (iTrack it = tracks->begin(); it != tracks->end(); ++it, ++sTrack) {
                  int h = (*it)->height();
                  ty += h;
                  if (y >= (ty-2)) {
                   
                        if ( (*it) == tracks->back() && y > ty ) {
                              //printf("tracks->back() && y > ty\n");
                        }
                        else if ( y > (ty+2) ) {
                              //printf(" y > (ty+2) \n");
                        }
                        else {
                              //printf("ogga ogga\n");
                        
                              break;
                              }
                   
                   
                   //&& y < (ty))
                   //     break;
                        }
                  }

            return;
            }
      mode = START_DRAG;

      switch (col) {
            case COL_RECORD:
                  {
                  bool val = !(t->recordFlag());
                  if (!t->isMidiTrack()) {
                        if (t->type() == Track::AUDIO_OUTPUT) {
                              if (val && t->recordFlag() == false) {
                                    muse->bounceToFile((AudioOutput*)t);
                                    }
                              audio->msgSetRecord((AudioOutput*)t, val);
                              if (!((AudioOutput*)t)->recFile())
                                    val = false;
                              else
                                    return;
                              }
                        song->setRecordFlag(t, val);
                        }
                  else
                        song->setRecordFlag(t, val);
                  }
                  break;
            case COL_NONE:
                  break;
            case COL_CLASS:
                  if (t->isMidiTrack())
                        classesPopupMenu(t, x, t->y() - ypos);
                  break;
            case COL_OPORT:
                  // Changed by Tim. p3.3.9
                  // Reverted.
                  if (button == QMouseEvent::LeftButton)
                        portsPopupMenu(t, x, t->y() - ypos);
                  else if (button == QMouseEvent::RightButton)
                        oportPropertyPopupMenu(t, x, t->y() - ypos);
                  //if(((button == QMouseEvent::LeftButton) && (t->type() == Track::AUDIO_SOFTSYNTH)) || (button == QMouseEvent::RightButton))
                  //  oportPropertyPopupMenu(t, x, t->y() - ypos);      
                  //else      
                  //if(button == QMouseEvent::LeftButton)
                  //  portsPopupMenu(t, x, t->y() - ypos);
                    
                  break;
            case COL_MUTE:
                  if (t->off())
                        t->setOff(false);
                  else
                        t->setMute(!t->mute());
                  song->update(SC_MUTE);
                  break;
            case COL_SOLO:
                  audio->msgSetSolo(t, !t->solo());
                  song->update(SC_SOLO);
                  break;

            case COL_NAME:
                  if (button == QMouseEvent::LeftButton) {
                        if (!shift) {
                              song->deselectTracks();
                              t->setSelected(true);
                              }
                        else
                              t->setSelected(!t->selected());
                        if (editTrack && editTrack != t)
                              returnPressed();
                        emit selectionChanged();
                        }
                  else if (button == QMouseEvent::RightButton) {
                        mode = NORMAL;
                        QPopupMenu* p = new QPopupMenu(this);
                        p->clear();
                        p->insertItem(*automation_clear_dataIcon, tr("Delete Track"), 0);
                        p->insertItem(QIconSet(*track_commentIcon), tr("Track Comment"), 1);
                        int n = p->exec(ev->globalPos(), 0);
                        if (n != -1) {
                              switch (n) {
                                    case 0:     // delete track
                                          song->removeTrack0(t);
                                          audio->msgUpdateSoloStates();
                                          break;

                                    case 1:     // show track comment
                                          {
                                          TrackComment* tc = new TrackComment(t, 0);
                                          tc->show();
                                          //QToolTip::add( this, "FOOOOOOOOOOOOO" );
                                          }
                                          break;

                                    default:
                                          printf("action %d\n", n);
                                          break;
                                    }

                              }
                        delete p;
                        }
                  break;

            case COL_TIMELOCK:
                  t->setLocked(!t->locked());
                  break;

            case COL_OCHANNEL:
                  {
                    int delta = 0;
                    if (button == QMouseEvent::RightButton) 
                      delta = 1;
                    else if (button == QMouseEvent::MidButton) 
                      delta = -1;
                    if (t->isMidiTrack()) 
                    {
                      MidiTrack* mt = dynamic_cast<MidiTrack*>(t);
                      if (mt == 0)
                      break;
                    
                      int channel = mt->outChannel();
                      channel += delta;
                      if(channel >= MIDI_CHANNELS)
                        channel = MIDI_CHANNELS - 1;
                      if(channel < 0)
                        channel = 0;
                      //if (channel != ((MidiTrack*)t)->outChannel()) 
                      if (channel != mt->outChannel()) 
                      {
                            // Changed by T356.
                            //mt->setOutChannel(channel);
                            audio->msgIdle(true);
                            //audio->msgSetTrackOutChannel(mt, channel);
                            mt->setOutChanAndUpdate(channel);
                            audio->msgIdle(false);
                            
                            /* --- I really don't like this, you can mess up the whole map "as easy as dell"
                            if (mt->type() == MidiTrack::DRUM) {//Change channel on all drum instruments
                                  for (int i=0; i<DRUM_MAPSIZE; i++)
                                        drumMap[i].channel = channel;
                                  }*/
                            
                            // may result in adding/removing mixer strip:
                            //song->update(-1);
                            //song->update(SC_CHANNELS);
                            song->update(SC_MIDI_CHANNEL);
                      }
                    }
                    else
                    {
                        if(t->type() != Track::AUDIO_SOFTSYNTH)
                        {
                          AudioTrack* at = dynamic_cast<AudioTrack*>(t);
                          if (at == 0)
                            break;
                    
                          int n = t->channels() + delta;
                          if (n > MAX_CHANNELS)
                                n = MAX_CHANNELS;
                          else if (n < 1)
                                n = 1;
                          if (n != t->channels()) {
                                audio->msgSetChannels(at, n);
                                song->update(SC_CHANNELS);
                                }
                        }         
                    }      
                  }
                  break;
          }        
      redraw();
      }

//---------------------------------------------------------
//   selectTrack
//---------------------------------------------------------
void TList::selectTrack(Track* tr)
      {
      song->deselectTracks();
      tr->setSelected(true);
      
      // By T356. Force a redraw for wave tracks, since it does not seem to happen.
      if(!tr->isMidiTrack())
        redraw();
      emit selectionChanged();
      }
//---------------------------------------------------------
//   mouseMoveEvent
//---------------------------------------------------------

void TList::mouseMoveEvent(QMouseEvent* ev)
      {
      if (ev->state() == 0) {
            int y = ev->y();
            int ty = -ypos;
            TrackList* tracks = song->tracks();
            iTrack it;
            for (it = tracks->begin(); it != tracks->end(); ++it) {
                  int h = (*it)->height();
                  ty += h;
                  if (y >= (ty-2)) { 
                        if ( (*it) == tracks->back() && y >= ty ) {
                              // outside last track don't change to splitVCursor
                        }
                        else if ( y > (ty+2) ) {
                              //printf(" y > (ty+2) \n");
                        }
                        else {
                              if (!resizeFlag) {
                                    resizeFlag = true;
                                    setCursor(QCursor(splitVCursor));
                                    }
                              break;
                              }
                        }
                  }
            if (it == tracks->end() && resizeFlag) {
                  setCursor(QCursor(arrowCursor));
                  resizeFlag = false;
                  }
            return;
            }
      curY      = ev->y();
      int delta = curY - startY;
      switch (mode) {
            case START_DRAG:
                  if (delta < 0)
                        delta = -delta;
                  if (delta <= 2)
                        break;
                  {
                  Track* t = y2Track(startY + ypos);
                  if (t == 0)
                        mode = NORMAL;
                  else {
                        mode = DRAG;
                        dragHeight = t->height();
                        sTrack     = song->tracks()->index(t);
                        setCursor(QCursor(sizeVerCursor));
                        redraw();
                        }
                  }
                  break;
            case NORMAL:
                  break;
            case DRAG:
                  redraw();
                  break;
            case RESIZE:
                  {
                    if(sTrack >= 0 && sTrack < song->tracks()->size())
                    {
                      Track* t = song->tracks()->index(sTrack);
                      if(t)
                      {
                        int h  = t->height() + delta;
                        startY = curY;
                        if (h < MIN_TRACKHEIGHT)
                              h = MIN_TRACKHEIGHT;
                        t->setHeight(h);
                        song->update(SC_TRACK_MODIFIED);
                      }  
                    }  
                  }
                  break;
            }
      }

//---------------------------------------------------------
//   mouseReleaseEvent
//---------------------------------------------------------

void TList::mouseReleaseEvent(QMouseEvent* ev)
      {
      if (mode == DRAG) {
            Track* t = y2Track(ev->y() + ypos);
            if (t) {
                  int dTrack = song->tracks()->index(t);
                  audio->msgMoveTrack(sTrack, dTrack);
                  }
            }
      if (mode != NORMAL) {
            mode = NORMAL;
            setCursor(QCursor(arrowCursor));
            redraw();
            }
      if (editTrack)
            editor->setFocus();
      adjustScrollbar();
      }

//---------------------------------------------------------
//   wheelEvent
//---------------------------------------------------------

void TList::wheelEvent(QWheelEvent* ev)
      {
      int x           = ev->x();
      int y           = ev->y();
      Track* t        = y2Track(y + ypos);
      if (t == 0) {
            emit redirectWheelEvent(ev);
            return;
            }
      TrackColumn col = TrackColumn(header->sectionAt(x));
      int delta       = ev->delta() / WHEEL_DELTA;
      ev->accept();

      switch (col) {
            case COL_RECORD:
            case COL_NONE:
            case COL_CLASS:
            case COL_NAME:
                  break;
            case COL_MUTE:
                  if (t->off())
                        t->setOff(false);
                  else
                        t->setMute(!t->mute());
                  song->update(SC_MUTE);
                  break;

            case COL_SOLO:
                  audio->msgSetSolo(t, !t->solo());
                  song->update(SC_SOLO);
                  break;

            case COL_TIMELOCK:
                  t->setLocked(!t->locked());
                  break;

            case COL_OPORT:
                  if (t->isMidiTrack()) {
                        MidiTrack* mt = (MidiTrack*)t;
                        int port = mt->outPort() + delta;

                        if (port >= MIDI_PORTS)
                              port = MIDI_PORTS-1;
                        else if (port < 0)
                              port = 0;
                        if (port != ((MidiTrack*)t)->outPort()) {
                              // Changed by T356.
                              //mt->setOutPort(port);
                              audio->msgIdle(true);
                              //audio->msgSetTrackOutPort(mt, port);
                              mt->setOutPortAndUpdate(port);
                              audio->msgIdle(false);
                              
                              song->update(SC_ROUTE);
                              }
                        }
                  break;

            case COL_OCHANNEL:
                  if (t->isMidiTrack()) {
                        MidiTrack* mt = (MidiTrack*)t;
                        int channel = mt->outChannel() + delta;

                        if (channel >= MIDI_CHANNELS)
                              channel = MIDI_CHANNELS-1;
                        else if (channel < 0)
                              channel = 0;
                        if (channel != ((MidiTrack*)t)->outChannel()) {
                              // Changed by T356.
                              //mt->setOutChannel(channel);
                              audio->msgIdle(true);
                              //audio->msgSetTrackOutChannel(mt, channel);
                              mt->setOutChanAndUpdate(channel);
                              audio->msgIdle(false);
                              
                              // may result in adding/removing mixer strip:
                              //song->update(-1);
                              song->update(SC_MIDI_CHANNEL);
                              }
                        }
                  else {
                        int n = t->channels() + delta;
                        if (n > MAX_CHANNELS)
                              n = MAX_CHANNELS;
                        else if (n < 1)
                              n = 1;
                        if (n != t->channels()) {
                              audio->msgSetChannels((AudioTrack*)t, n);
                              song->update(SC_CHANNELS);
                              }
                        }
                  break;
            default:
                  break;
            }
      }

//---------------------------------------------------------
//   writeStatus
//---------------------------------------------------------

void TList::writeStatus(int level, Xml& xml, const char* name) const
      {
      xml.tag(level++, name);
      header->writeStatus(level, xml);
      xml.etag(level, name);
      }

//---------------------------------------------------------
//   readStatus
//---------------------------------------------------------

void TList::readStatus(Xml& xml, const char* name)
      {
      for (;;) {
            Xml::Token token(xml.parse());
            const QString& tag(xml.s1());
            switch (token) {
                  case Xml::Error:
                  case Xml::End:
                        return;
                  case Xml::TagStart:
                        if (tag == header->name())
                              header->readStatus(xml);
                        else
                              xml.unknown("Tlist");
                        break;
                  case Xml::TagEnd:
                        if (tag == name)
                              return;
                  default:
                        break;
                  }
            }
      }

//---------------------------------------------------------
//   setYPos
//---------------------------------------------------------

void TList::setYPos(int y)
      {
      int delta  = ypos - y;         // -  -> shift up
      ypos  = y;
      if (pm.isNull())
            return;
      if (!pmValid) {
            redraw();
            return;
            }
      int w = width();
      int h = height();
      QRect r;
      if (delta >= h || delta <= -h)
            r = QRect(0, 0, w, h);
      else if (delta < 0) {   // shift up
            bitBlt(&pm,  0, 0, &pm, 0, -delta, w, h + delta, CopyROP, true);
            r = QRect(0, h + delta, w, -delta);
            }
      else {                  // shift down
            bitBlt(&pm,  0, delta, &pm, 0, 0, w, h-delta, CopyROP, true);
            r = QRect(0, 0, w, delta);
            }
      paint(r);
      update();
      }

//---------------------------------------------------------
//   resizeEvent
//---------------------------------------------------------

void TList::resizeEvent(QResizeEvent* ev)
      {
      pm.resize(ev->size());
      pmValid = false;
      }

//---------------------------------------------------------
//   classesPopupMenu
//---------------------------------------------------------

void TList::classesPopupMenu(Track* t, int x, int y)
      {
      QPopupMenu p(this);
      p.clear();
      p.insertItem(*addtrack_addmiditrackIcon, tr("Midi"), Track::MIDI);
      p.insertItem(*addtrack_drumtrackIcon, tr("Drum"), Track::DRUM);
      int n = p.exec(mapToGlobal(QPoint(x, y)), 0);

      if (n == -1)
            return;

      if (Track::TrackType(n) == Track::MIDI && t->type() == Track::DRUM) {
            //
            //    Drum -> Midi
            //
            audio->msgIdle(true);
            PartList* pl = t->parts();
            MidiTrack* m = (MidiTrack*) t;
            for (iPart ip = pl->begin(); ip != pl->end(); ++ip) {
                  EventList* el = ip->second->events();
                  for (iEvent ie = el->begin(); ie != el->end(); ++ie) {
                        Event ev = ie->second;
                        if(ev.type() == Note)
                        {
                          int pitch = ev.pitch();
                          // Changed by T356.
                          // Tested: Notes were being mixed up switching back and forth between midi and drum.
                          //pitch = drumMap[pitch].anote;
                          pitch = drumMap[pitch].enote;
                          
                          ev.setPitch(pitch);
                        }
                        else
                        if(ev.type() == Controller)
                        {
                          int ctl = ev.dataA();
                          // Is it a drum controller event, according to the track port's instrument?
                          MidiController *mc = midiPorts[m->outPort()].drumController(ctl);
                          if(mc)
                            // Change the controller event's index into the drum map to an instrument note.
                            ev.setA((ctl & ~0xff) | drumMap[ctl & 0x7f].enote);
                        }
                          
                      }
                  }
            t->setType(Track::MIDI);
            audio->msgIdle(false);
            }
      else if (Track::TrackType(n) == Track::DRUM && t->type() == Track::MIDI) {
            //
            //    Midi -> Drum
            //
            bool change = QMessageBox::question(this, tr("Update drummap?"),
                           tr("Do you want to use same port and channel for all instruments in the drummap?"),
                           tr("&Yes"), tr("&No"), QString::null, 0, 1);
            
            audio->msgIdle(true);
            // Delete all port controller events.
            //audio->msgChangeAllPortDrumCtrlEvents(false);
            song->changeAllPortDrumCtrlEvents(false);
            
            if (!change) {
                  MidiTrack* m = (MidiTrack*) t;
                  for (int i=0; i<DRUM_MAPSIZE; i++) {
                        drumMap[i].channel = m->outChannel();
                        drumMap[i].port    = m->outPort();
                        }
                  }

            //audio->msgIdle(true);
            PartList* pl = t->parts();
            MidiTrack* m = (MidiTrack*) t;
            for (iPart ip = pl->begin(); ip != pl->end(); ++ip) {
                  EventList* el = ip->second->events();
                  for (iEvent ie = el->begin(); ie != el->end(); ++ie) {
                        Event ev = ie->second;
                        if (ev.type() == Note)
                        {
                          int pitch = ev.pitch();
                          pitch = drumInmap[pitch];
                          ev.setPitch(pitch);
                        }  
                        else
                        {
                          if(ev.type() == Controller)
                          {
                            int ctl = ev.dataA();
                            // Is it a drum controller event, according to the track port's instrument?
                            MidiController *mc = midiPorts[m->outPort()].drumController(ctl);
                            if(mc)
                              // Change the controller event's instrument note to an index into the drum map.
                              ev.setA((ctl & ~0xff) | drumInmap[ctl & 0x7f]);
                          }
                          
                        }
                        
                      }
                  }
            t->setType(Track::DRUM);
            
            // Add all port controller events.
            //audio->msgChangeAllPortDrumCtrlEvents(true);
            song->changeAllPortDrumCtrlEvents(true);
            
            audio->msgIdle(false);
            }
      }

