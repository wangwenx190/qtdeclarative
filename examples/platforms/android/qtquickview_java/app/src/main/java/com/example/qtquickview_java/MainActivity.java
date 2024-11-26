// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause
package com.example.qtquickview_java;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;
import android.content.res.Configuration;
import android.graphics.Color;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;
import org.qtproject.qt.android.QtQmlStatus;
import org.qtproject.qt.android.QtQmlStatusChangeListener;
import org.qtproject.qt.android.QtQuickView;
import org.qtproject.example.qtquickview.QmlModule.Main;
import org.qtproject.example.qtquickview.QmlModule.Second;
import java.util.HashMap;
import java.util.Map;

// Implement QtQuickView StatusChangeListener interface to get status updates
// from the underlying QQuickView
public class MainActivity extends AppCompatActivity implements QtQmlStatusChangeListener {

    private static final String TAG = "myTag";
    private final Colors m_colors = new Colors();
    private final Map<QtQmlStatus, String> m_statusNames = new HashMap<QtQmlStatus, String>()  {{
        put(QtQmlStatus.READY, " READY");
        put(QtQmlStatus.LOADING, " LOADING");
        put(QtQmlStatus.ERROR, " ERROR");
        put(QtQmlStatus.NULL, " NULL");
    }};
    private int m_qmlButtonSignalListenerId;
    private LinearLayout m_mainLinear;

    private FrameLayout m_firstQmlFrameLayout;
    private FrameLayout m_secondQmlFrameLayout;

    //! [qmlContent]
    private final Main m_firstQmlContent = new Main();
    private final Second m_secondQmlContent = new Second();
    //! [qmlContent]
    private RelativeLayout m_androidControlsLayout;
    private TextView m_qmlViewBackgroundText;
    private TextView m_qmlStatus;
    private SwitchCompat m_switch;
    private RelativeLayout m_colorBox;

    //! [onCreate]
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        m_mainLinear = findViewById(R.id.mainLinear);
        m_qmlViewBackgroundText = findViewById(R.id.qmlViewBackgroundText);
        m_qmlStatus = findViewById(R.id.qmlStatusText);
        m_androidControlsLayout = findViewById(R.id.javaRelative);
        m_colorBox = findViewById(R.id.qmlColorBox);
        m_switch = findViewById(R.id.disconnectQmlListenerSwitch);
        m_switch.setOnClickListener(view -> switchListener());
        //! [m_qtQuickView]
        QtQuickView m_firstQuickView = new QtQuickView(this);
        QtQuickView m_secondQuickView = new QtQuickView(this);
        //! [m_qtQuickView]

        // Set status change listener for m_qmlView
        // listener implemented below in OnStatusChanged
        //! [setStatusChangeListener]
        m_firstQmlContent.setStatusChangeListener(this);
        m_secondQmlContent.setStatusChangeListener(this);
        //! [setStatusChangeListener]
        //! [layoutParams]
        final ViewGroup.LayoutParams params = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        m_firstQmlFrameLayout = findViewById(R.id.firstQmlFrame);
        m_firstQmlFrameLayout.addView(m_firstQuickView, params);
        m_secondQmlFrameLayout = findViewById(R.id.secondQmlFrame);
        m_secondQmlFrameLayout.addView(m_secondQuickView, params);
        //! [layoutParams]
        //! [loadContent]
        m_firstQuickView.loadContent(m_firstQmlContent);
        m_secondQuickView.loadContent(m_secondQmlContent);
        //! [loadContent]

        Button m_changeColorButton = findViewById(R.id.changeQmlColorButton);
        m_changeColorButton.setOnClickListener(view -> onClickListener());
        Button m_rotateQmlGridButton = findViewById(R.id.rotateQmlGridButton);
        m_rotateQmlGridButton.setOnClickListener(view -> rotateQmlGrid());

        // Check target device orientation on launch
        handleOrientationChanges();
    }
    //! [onCreate]

    @Override
    public void onConfigurationChanged(@NonNull Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        handleOrientationChanges();
    }

    private void handleOrientationChanges() {
        // When specific target device display configurations (listed in AndroidManifest.xml
        // android:configChanges) change, get display metrics and make needed changes to UI
        DisplayMetrics displayMetrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getMetrics(displayMetrics);
        ViewGroup.LayoutParams firstQmlFrameLayoutParams = m_firstQmlFrameLayout.getLayoutParams();
        ViewGroup.LayoutParams secondQmlFrameLayoutParams =
                m_secondQmlFrameLayout.getLayoutParams();
        ViewGroup.LayoutParams linearLayoutParams = m_androidControlsLayout.getLayoutParams();

        if (displayMetrics.heightPixels > displayMetrics.widthPixels) {
            m_mainLinear.setOrientation(LinearLayout.VERTICAL);
            firstQmlFrameLayoutParams.width = ViewGroup.LayoutParams.MATCH_PARENT;
            firstQmlFrameLayoutParams.height = 0;
            secondQmlFrameLayoutParams.width = ViewGroup.LayoutParams.MATCH_PARENT;
            secondQmlFrameLayoutParams.height = 0;
            linearLayoutParams.width = ViewGroup.LayoutParams.MATCH_PARENT;
            linearLayoutParams.height = 0;
        } else {
            m_mainLinear.setOrientation(LinearLayout.HORIZONTAL);
            firstQmlFrameLayoutParams.width = 0;
            firstQmlFrameLayoutParams.height = ViewGroup.LayoutParams.MATCH_PARENT;
            secondQmlFrameLayoutParams.width = 0;
            secondQmlFrameLayoutParams.height = ViewGroup.LayoutParams.MATCH_PARENT;
            linearLayoutParams.width = 0;
            linearLayoutParams.height = ViewGroup.LayoutParams.MATCH_PARENT;
        }
        m_firstQmlFrameLayout.setLayoutParams(firstQmlFrameLayoutParams);
        m_secondQmlFrameLayout.setLayoutParams(secondQmlFrameLayoutParams);
        m_androidControlsLayout.setLayoutParams(linearLayoutParams);
    }

    //! [onClickListener]
    public void onClickListener() {
        // Set the QML view root object property "colorStringFormat" value to
        // color from Colors.getColor()
        m_firstQmlContent.setColorStringFormat(m_colors.getColor());
        updateColorDisplay();
    }
    private void updateColorDisplay() {
        String qmlBackgroundColor = m_firstQmlContent.getColorStringFormat();
        // Display the QML View background color code
        m_qmlViewBackgroundText.setText(qmlBackgroundColor);

        // Display the QML View background color in a view
        // if qmlBackGroundColor is not null
        if (qmlBackgroundColor != null) {
            m_colorBox.setBackgroundColor(Color.parseColor(qmlBackgroundColor));
        }
    }
    //! [onClickListener]

    public void switchListener() {
        // Disconnect QML button signal listener if switch is On using the saved signal listener Id
        // and connect it again if switch is turned off
        if (m_switch.isChecked()) {
            m_qmlButtonSignalListenerId =
                    m_firstQmlContent.connectOnClickedListener((String name, Void v) -> {
                        m_androidControlsLayout.setBackgroundColor(Color.parseColor(
                                m_colors.getColor()
                        ));
                    });
        } else {
            //! [disconnect qml signal listener]
            m_firstQmlContent.disconnectSignalListener(m_qmlButtonSignalListenerId);
            //! [disconnect qml signal listener]
        }
    }

    //! [onStatusChanged]
    @Override
    public void onStatusChanged(QtQmlStatus qtQmlStatus) {
        Log.i(TAG, "Status of QtQuickView: " + qtQmlStatus);

        // Show current QML View status in a textview
        m_qmlStatus.setText(getString(R.string.qml_view_status, m_statusNames.get(qtQmlStatus)));
        updateColorDisplay();

        // Connect signal listener to "onClicked" signal from main.qml
        // addSignalListener returns int which can be used later to identify the listener
        //! [qml signal listener]
        if (qtQmlStatus == QtQmlStatus.READY && m_switch.isChecked()) {
            m_qmlButtonSignalListenerId =
                    m_firstQmlContent.connectOnClickedListener((String name, Void v) -> {
                        Log.i(TAG, "QML button clicked");
                        m_androidControlsLayout.setBackgroundColor(Color.parseColor(
                                m_colors.getColor()
                        ));
                    });
        }
        //! [qml signal listener]
    }
    //! [onStatusChanged]
    //! [gridRotate]
    private void rotateQmlGrid() {
        Integer previousGridRotation = m_secondQmlContent.getGridRotation();
        if (previousGridRotation != null) {
            m_secondQmlContent.setGridRotation(previousGridRotation + 45);
        }
    }
    //! [gridRotate]
}
