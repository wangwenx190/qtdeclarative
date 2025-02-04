import QtQuick
import QtQuick.VectorImage

Rectangle { 
    id: topLevelItem
    width: 800
    height: 200

    ListModel {
        id: renderers
        ListElement { renderer: VectorImage.GeometryRenderer; src: "../shared/svg/animateScaleTransform.svg" }
        ListElement { renderer: VectorImage.GeometryRenderer; src: "../shared/svg/animateScaleTransform2.svg" }
        ListElement { renderer: VectorImage.CurveRenderer; src: "../shared/svg/animateScaleTransform.svg" }
        ListElement { renderer: VectorImage.CurveRenderer; src: "../shared/svg/animateScaleTransform2.svg" }
    }

    Row {
        Repeater {
            model: renderers

            VectorImage {
                layer.enabled: true
                layer.samples: 4
                source: src
                preferredRendererType: renderer
            }
        }
    }
}
