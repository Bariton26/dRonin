/**
 ******************************************************************************
 *
 * @file       configairframetwidget.h
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2012.
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup ConfigPlugin Config Plugin
 * @{
 * @brief Airframe configuration panel
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>
 */
#ifndef CONFIGVEHICLETYPEWIDGET_H
#define CONFIGVEHICLETYPEWIDGET_H

#include "ui_airframe.h"
#include "../uavobjectwidgetutils/configtaskwidget.h"
#include "extensionsystem/pluginmanager.h"
#include "uavobjects/uavobjectmanager.h"
#include "uavobjects/uavobject.h"
#include "uavtalk/telemetrymanager.h"

#include "cfg_vehicletypes/configccpmwidget.h"
#include "cfg_vehicletypes/configfixedwingwidget.h"
#include "cfg_vehicletypes/configmultirotorwidget.h"
#include "cfg_vehicletypes/configgroundvehiclewidget.h"
#include "cfg_vehicletypes/vehicleconfig.h"

#include <QWidget>
#include <QList>
#include <QItemDelegate>

class Ui_Widget;

class ConfigVehicleTypeWidget : public ConfigTaskWidget
{
    Q_OBJECT
private:
    enum {
        AIRFRAME_FIXED_WING,
        AIRFRAME_MULTIROTOR,
        AIRFRAME_HELICOPTER,
        AIRFRAME_GROUND,
        AIRFRAME_CUSTOM
    };

public:
    ConfigVehicleTypeWidget(QWidget *parent = 0);
    ~ConfigVehicleTypeWidget();

    static QStringList getChannelDescriptions();

private:
    Ui_AircraftWidget *m_aircraft;

    ConfigCcpmWidget *m_heli;
    ConfigFixedWingWidget *m_fixedwing;
    ConfigMultiRotorWidget *m_multirotor;
    ConfigGroundVehicleWidget *m_groundvehicle;

    void updateCustomAirframeUI();
    void addToDirtyMonitor();
    void resetField(UAVObjectField *field);

    // void setMixerChannel(int channelNumber, bool channelIsMotor, QList<double> vector);

    QStringList channelNames;
    QStringList mixerTypes;
    QStringList mixerVectors;

    QGraphicsSvgItem *quad;
    UAVObject::Metadata accInitialData;
    SystemSettings::AirframeTypeOptions frameType;

private slots:

    virtual void refreshWidgetsValues(UAVObject *o = NULL);
    virtual void updateObjectsFromWidgets();

    void setComboCurrentIndex(QComboBox *box, int index);

    void doSetupAirframeUI(int frameType);
    void setupAirframeUI(SystemSettings::AirframeTypeOptions frameType);

    void toggleAileron2(int index);
    void toggleElevator2(int index);
    void toggleRudder2(int index);
    void switchAirframeType(int index);

    void reverseMultirotorMotor();

    void bnLevelTrim_clicked();
    void bnServoTrim_clicked();

protected:
    void showEvent(QShowEvent *event);
    void resizeEvent(QResizeEvent *event);
};

class SpinBoxDelegate : public QItemDelegate
{
    Q_OBJECT

public:
    SpinBoxDelegate(QObject *parent = 0);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const;

    void setEditorData(QWidget *editor, const QModelIndex &index) const;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const;

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const;
};

#endif // CONFIGVEHICLETYPEWIDGET_H
