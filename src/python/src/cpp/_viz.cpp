/**
 * Copyright (c) 2021, Ouster, Inc.
 * All rights reserved.
 *
 * @file
 * @brief pybind wrappers for the ouster simple viz library
 *
 * PoC for exposing the opengl visualizer in Python.
 */
#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <atomic>
#include <csignal>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ouster/impl/build.h"
#include "ouster/lidar_scan.h"
#include "ouster/point_viz.h"
#include "ouster/types.h"

namespace py = pybind11;
using namespace ouster;

template <typename T, int F>
static void check_array(const py::array_t<T, F>& array, size_t size = 0,
                        size_t dims = 0, char storage = 'X') {
    if (size && static_cast<size_t>(array.size()) != size)
        throw std::invalid_argument("Expected array of size: " +
                                    std::to_string(size));

    if (dims && static_cast<size_t>(array.ndim()) != dims)
        throw std::invalid_argument("Expected an array of dimension: " +
                                    std::to_string(dims));

    if (storage == 'F' && !(array.flags() & py::array::f_style))
        throw std::invalid_argument("Expected a F_CONTIGUOUS array");

    if (storage == 'C' && !(array.flags() & py::array::c_style))
        throw std::invalid_argument("Expected a C_CONTIGUOUS array");
}

template <size_t N>
static void tuple_to_float_array(std::array<float, N>& dst,
                                 const py::tuple& tuple) {
    if (tuple.size() > N)
        throw std::invalid_argument("Expected a tuple of size <= " +
                                    std::to_string(N));
    try {
        for (size_t i = 0; i < tuple.size(); i++) {
            dst[i] = tuple[i].cast<float>();
        }
    } catch (const py::cast_error&) {
        throw py::type_error("Expected a tuple of floats");
    }
}

// pybind11 size type changed since v2.0
using pysize = decltype(py::array{}.size());

// matrices passed to eigen must be converted to col-major
using pymatrixd =
    py::array_t<double, py::array::f_style | py::array::forcecast>;

PYBIND11_PLUGIN(_viz) {
    py::module m("_viz", R"(
    PointViz bindings generated by pybind11.

    This module is generated from the C++ code and not meant to be used directly.
    )");

    // turn off signatures in docstrings: mypy stubs provide better types
    py::options options;
    options.disable_function_signatures();

    py::class_<viz::PointViz>(m, "PointViz")
        .def(py::init<const std::string&, bool, int, int>(), py::arg("name"),
             py::arg("fix_aspect") = false, py::arg("window_width") = 800,
             py::arg("window_height") = 600)

        .def(
            "run",
            [](viz::PointViz& self) {
                // acquire gil every n frames to check for signals
                const int check_every = 10;
                self.running(true);
                self.visible(true);
                while (self.running()) {
                    if (PyErr_CheckSignals() != 0)
                        throw py::error_already_set();
                    py::gil_scoped_release release;
                    for (int i = 0; i < check_every; i++) self.run_once();
                }
                self.visible(false);
            },
            R"(
             Run the visualizer rendering loop.

             Must be called from the main thread. Will return when ``running(False)`` is
             called from another thread or when the visualizer window is closed.
        )")

        .def("run_once", &viz::PointViz::run_once,
             "Run one iteration of the main loop for rendering and input "
             "handling.")

        .def("running", py::overload_cast<>(&viz::PointViz::running),
             "Check if the rendering loop is running.")

        .def("running", py::overload_cast<bool>(&viz::PointViz::running),
             "Shut down the visualizer and break out of the rendering loop.")

        .def("update", &viz::PointViz::update,
             "Show updated data in the next rendered frame.")

        // misc
        .def(
            "push_key_handler",
            [](viz::PointViz& self,
               std::function<bool(const viz::WindowCtx&, int, int)> f) {
                // pybind11 doesn't seem to deal with the rvalue ref arg
                // pybind11 already handles acquiring the GIL in the callback
                self.push_key_handler(std::move(f));
            },
            "Add a callback for handling keyboard input.")

        // control scene
        .def_property_readonly("camera", &viz::PointViz::camera,
                               py::return_value_policy::reference_internal,
                               "Get a reference to the camera controls.")

        .def_property_readonly("target_display", &viz::PointViz::target_display,
                               py::return_value_policy::reference_internal,
                               "Get a reference to the target display.")

        .def("add",
             py::overload_cast<const std::shared_ptr<viz::Cloud>&>(
                 &viz::PointViz::add),
             R"(
             Add an object to the scene.

             Args:
                 obj: A cloud, label, image or cuboid.)")
        .def("add", py::overload_cast<const std::shared_ptr<viz::Cuboid>&>(
                        &viz::PointViz::add))
        .def("add", py::overload_cast<const std::shared_ptr<viz::Label>&>(
                        &viz::PointViz::add))
        .def("add", py::overload_cast<const std::shared_ptr<viz::Image>&>(
                        &viz::PointViz::add))
        .def("remove",
             py::overload_cast<const std::shared_ptr<viz::Cloud>&>(
                 &viz::PointViz::remove),
             R"(
             Remove an object from the scene.

             Args:
                 obj: A cloud, label, image or cuboid.

             Returns:
                 True if the object was in the scene and was removed.
             )")
        .def("remove", py::overload_cast<const std::shared_ptr<viz::Cuboid>&>(
                           &viz::PointViz::remove))
        .def("remove", py::overload_cast<const std::shared_ptr<viz::Label>&>(
                           &viz::PointViz::remove))
        .def("remove", py::overload_cast<const std::shared_ptr<viz::Image>&>(
                           &viz::PointViz::remove));

    m.def(
        "add_default_controls",
        [](viz::PointViz& viz) { viz::add_default_controls(viz); },
        "Add default keyboard and mouse bindings to a visualizer instance.");

    py::class_<viz::WindowCtx>(m, "WindowCtx", "Context for input callbacks.")
        .def_readonly("lbutton_down", &viz::WindowCtx::lbutton_down,
                      "True if the left mouse button is held")
        .def_readonly("mbutton_down", &viz::WindowCtx::mbutton_down,
                      "True if the middle mouse button is held")
        .def_readonly("mouse_x", &viz::WindowCtx::mouse_x,
                      "Current mouse x position")
        .def_readonly("mouse_y", &viz::WindowCtx::mouse_y,
                      "Current mouse y position")
        .def_readonly("viewport_width", &viz::WindowCtx::viewport_width,
                      "Current viewport width in pixels")
        .def_readonly("viewport_height", &viz::WindowCtx::viewport_height,
                      "Current viewport height in pixels");

    py::class_<viz::Camera>(m, "Camera",
                            "Controls the camera view and projection.")
        .def("reset", &viz::Camera::reset, "Reset the camera view and fov.")
        .def("yaw", &viz::Camera::yaw, py::arg("degrees"),
             "Orbit the camera left or right about the camera target.")
        .def("pitch", &viz::Camera::pitch, py::arg("degrees"),
             "Pitch the camera up or down.")
        .def("dolly", &viz::Camera::dolly, py::arg("amount"),
             "Move the camera towards or away from the target.")
        .def("dolly_xy", &viz::Camera::dolly_xy, py::arg("x"), py::arg("y"),
             R"(
             Move the camera in the XY plane of the camera view.

             Args:
                 x: horizontal offset
                 y: vertical offset
             )")
        .def("set_fov", &viz::Camera::set_fov, py::arg("degrees"),
             "Set the diagonal field of view.")
        .def("set_orthographic", &viz::Camera::set_orthographic,
             py::arg("state"), "Use an orthographic or perspective projection.")
        .def("set_proj_offset", &viz::Camera::set_proj_offset, py::arg("x"),
             py::arg("y"),
             R"(
             Set the 2d position of camera target in the viewport.

             Args:
                 x: horizontal position in in normalized coordinates [-1, 1]
                 y: vertical position in in normalized coordinates [-1, 1]
             )");

    py::class_<viz::TargetDisplay>(
        m, "TargetDisplay", "Manages the state of the camera target display.")
        .def("enable_rings", &viz::TargetDisplay::enable_rings,
             py::arg("state"), "Enable or disable distance ring display.")
        .def("set_ring_size", &viz::TargetDisplay::set_ring_size, py::arg("n"),
             "Set the distance between rings.");

    py::class_<viz::Cloud, std::shared_ptr<viz::Cloud>>(m, "Cloud",
                                                        R"(
             Manages the state of a point cloud.

             Each point cloud consists of n points with w poses. The ith point will be
             transformed by the (i % w)th pose. For example for 2048 x 64 Ouster lidar
             point cloud, we may have w = 2048 poses and n = 2048 * 64 = 131072 points.
 
             We also keep track of a per-cloud pose to efficiently transform the
             whole point cloud without having to update all ~2048 poses.
             )")
        .def(
            "__init__",
            [](viz::Cloud& self, size_t n) { new (&self) viz::Cloud{n}; },
            py::arg("n"),
            R"(
                 ``def __init__(self, n_points: int) -> None:``

                 Unstructured point cloud for visualization.

                 Call set_xyz() to update

                 Args:
                    n: number of points
                    extrinsic: sensor extrinsic calibration. 4x4 column-major
                               homogeneous transformation matrix
             )")
        .def(
            "__init__",
            [](viz::Cloud& self, const sensor::sensor_info& info) {
                const auto xyz_lut = make_xyz_lut(info);

                // make_xyz_lut still outputs doubles
                Eigen::Array<float, Eigen::Dynamic, 3> direction =
                    xyz_lut.direction.cast<float>();
                Eigen::Array<float, Eigen::Dynamic, 3> offset =
                    xyz_lut.direction.cast<float>();

                viz::mat4d extrinsica;
                std::copy(info.extrinsic.data(), info.extrinsic.data() + 16,
                          extrinsica.data());

                new (&self)
                    viz::Cloud{info.format.columns_per_frame,
                               info.format.pixels_per_column, direction.data(),
                               offset.data(), extrinsica};
            },
            py::arg("metadata"),
            R"(
                 ``def __init__(self, si: SensorInfo) -> None:``

                 Structured point cloud for visualization.

                 Call set_range() to update

                 Args:
                    info: sensor metadata
             )")
        .def(
            "set_range",
            [](viz::Cloud& self, py::array_t<uint32_t> range) {
                check_array(range, self.get_size(), 2, 'C');
                self.set_range(range.data());
            },
            py::arg("range"),
            R"(
                Set the range values.

                Args:
                  range: array of at least as many elements as there are points,
                         representing the range of the points
              )")
        .def(
            "set_key",
            [](viz::Cloud& self, py::array_t<float> key) {
                check_array(key, self.get_size(), 0, 'C');
                self.set_key(key.data());
            },
            py::arg("key"),
            R"(
                 Set the key values, used for colouring.

                 Args:
                    key: array of at least as many elements as there are
                         points, preferably normalized between 0 and 1
             )")
        .def(
            "set_mask",
            [](viz::Cloud& self, py::array_t<float> mask) {
                check_array(mask, self.get_size() * 4, 0, 'C');
                if (mask.ndim() != 2 && mask.ndim() != 3)
                    throw std::invalid_argument(
                        "Expected an array of dimensions: 2 or 3");
                self.set_mask(mask.data());
            },
            py::arg("mask"),
            R"(
                 Set the RGBA mask values, used as an overlay on top of the key.

                 Args:
                    mask: array of at least 4x as many elements as there
                          are points, preferably normalized between 0 and 1
             )")
        .def(
            "set_xyz",
            [](viz::Cloud& self, py::array_t<float> xyz) {
                check_array(xyz, self.get_size() * 3, 0);
                self.set_xyz(xyz.data());
            },
            py::arg("xyz"),
            R"(
                 Set the XYZ values.

                 Args:
                    xyz: array of exactly 3n where n is number of points, so
                         that the xyz position of the ith point is ``i``,
                         ``i + n``, ``i + 2n``
             )")
        .def(
            "set_pose",
            [](viz::Cloud& self, pymatrixd pose) {
                check_array(pose, 16, 2, 'F');
                viz::mat4d posea;
                std::copy(pose.data(), pose.data() + 16, posea.data());
                self.set_pose(posea);
            },
            py::arg("pose"),
            R"(
                 Set the ith point cloud pose.

                 Args:
                    pose: 4x4 column-major homogeneous transformation matrix
             )")
        .def("set_point_size", &viz::Cloud::set_point_size, py::arg("size"),
             R"(
            Set point size.

            Args:
                size: point size
        )")
        .def(
            "set_palette",
            [](viz::Cloud& self, py::array_t<float> buf) {
                check_array(buf, 0, 2, 'C');
                if (buf.shape(1) != 3)
                    throw std::invalid_argument("Expected a N x 3 array");
                self.set_palette(buf.data(), buf.shape(0));
            },
            py::arg("palette"),
            R"(
            Set the point cloud color palette.

            Args:
                palette: the new palette to use, must have size 3*palette_size
        )");

    py::class_<viz::Image, std::shared_ptr<viz::Image>>(
        m, "Image", "Manages the state of an image.")
        .def(py::init<>())
        .def(
            "set_image",
            [](viz::Image& self, py::array_t<float> image) {
                check_array(image, 0, 2, 'C');
                self.set_image(image.shape(1), image.shape(0), image.data());
            },
            py::arg("image"), R"(
                 Set the image data.

                 Args:
                    image: 2D array with image data
             )")
        .def(
            "set_mask",
            [](viz::Image& self, py::array_t<float> buf) {
                check_array(buf, 0, 3, 'C');
                if (buf.shape(2) != 4)
                    throw std::invalid_argument("Expected a M x N x 4 array");
                self.set_mask(buf.shape(1), buf.shape(0), buf.data());
            },
            py::arg("mask"),
            R"(
                 Set the RGBA mask.

                 Args:
                    mask: M x N x 4 array with RGBA mask
             )")
        .def("set_position", &viz::Image::set_position, py::arg("x_min"),
             py::arg("x_max"), py::arg("y_min"), py::arg("y_max"),
             R"(
            Set the display position of the image.

            Coordinates are {x_min, x_max, y_max, y_min} in sort-of normalized
            screen coordinates: y is in [-1, 1], and x uses the same scale
            (i.e. window width is ignored). This is currently just the same
            method the previous hard-coded 'image_frac' logic was using; I
            believe it was done this way to allow scaling with the window
            while maintaining the aspect ratio.

            Args:
                x_min: min x
                x_max: max x
                y_min: min y
                y_max: max y
        )")
        .def("set_hshift", &viz::Image::set_hshift, py::arg("hshift"),
             R"(
            Set horizontal shift in normalized viewport screen width coordinate.

            This may be used to "snap" images to the left/right screen edges.

            Some example values:
              ``0`` - default, image is centered horizontally on the screen
              ``-0.5`` - image moved to the left for the 1/4 of a horizontal viewport
              ``-1`` - image moved to the left for the 1/2 of a horizontal viewport
              ``+1`` - image moved to the right for the 1/2 of a horizontal viewport
              ``+0.5`` - image moved to the right for the 1/4 of a horizontal viewport
        )");

    py::class_<viz::Cuboid, std::shared_ptr<viz::Cuboid>>(
        m, "Cuboid", "Manages the state of a single cuboid.")
        .def(
            "__init__",
            [](viz::Cuboid& self, pymatrixd pose, py::tuple rgba) {
                check_array(pose, 16, 2, 'F');
                viz::mat4d posea;
                std::copy(pose.data(), pose.data() + 16, posea.data());

                viz::vec4f ar{0.0, 0.0, 0.0, 1.0};
                tuple_to_float_array(ar, rgba);

                new (&self) viz::Cuboid{posea, ar};
            },
            py::arg("pose"), py::arg("rgba"),
            R"(
                 Creates cuboid.

                 Args:
                    pose: 4x4 pose matrix
                    rgba: 4 value tuple of RGBA color
             )")
        .def(
            "set_transform",
            [](viz::Cuboid& self, pymatrixd pose) {
                check_array(pose, 16, 2, 'F');
                viz::mat4d posea;
                std::copy(pose.data(), pose.data() + 16, posea.data());
                self.set_transform(posea);
            },
            py::arg("pose"),
            R"(
                 Set the transform defining the cuboid.

                 Applied to a unit cube centered at the origin.

                 Args:
                    pose: 4x4 pose matrix
             )")
        .def(
            "set_rgba",
            [](viz::Cuboid& self, py::tuple rgba) {
                viz::vec4f ar{0.0, 0.0, 0.0, 1.0};
                tuple_to_float_array(ar, rgba);
                self.set_rgba(ar);
            },
            py::arg("rgba"),
            R"(
            Set the color of the cuboid.

            Args:
                rgba: 4 value tuple of RGBA color
        )");

    py::class_<viz::Label, std::shared_ptr<viz::Label>>(
        m, "Label", "Manages the state of a text label.")
        .def(
            "__init__",
            [](viz::Label& self, const std::string& text, double x, double y,
               double z) {
                new (&self) viz::Label{text, {x, y, z}};
            },
            py::arg("text"), py::arg("x"), py::arg("y"), py::arg("z"),
            R"(
                 ``def __init__(self, text: str, x: float, y: float, z: float) -> None:``

                 Creates 3D Label.

                 Args:
                    text: label text
                    x,y,z: label location
             )")
        .def(py::init<const std::string&, float, float, bool, bool>(),
             py::arg("text"), py::arg("x"), py::arg("y"),
             py::arg("align_right") = false, py::arg("align_top") = false,
             R"(
                 ``def __init__(self, text: str, x: float, y: float, align_right: bool = ..., align_top: bool = ...) -> None:``

                 Creates 2D Label.

                 Args:
                    text: label text
                    x,y: label 2D location in screen coords ``[0..1]``, corresponding to top left corner of label
                    align_right: if ``True`` - anchor point of the label is the right side
                    align_top: if ``True`` - anchor point of the label is the top side
             )")
        .def("set_text", &viz::Label::set_text, py::arg("text"),
             R"(
            Update label text.

            Args:
                text: new text to display
        )")
        .def(
            "set_position",
            [](viz::Label& self, double x, double y, double z) {
                self.set_position({x, y, z});
            },
            py::arg("x"), py::arg("y"), py::arg("z"),
            R"(
                ``def set_position(self, x: float, y: float, z: float) -> None:``

                 Set label position. Position correspnods to top left (viewer's left) of label.

                 Args:
                    x,y,z: label position in 3D
            )")
        .def("set_position",
             py::overload_cast<float, float, bool, bool>(
                 &viz::Label::set_position),
             py::arg("x"), py::arg("y"), py::arg("align_right") = false,
             py::arg("align_top") = false,
             R"(
                 ``def set_position(self, x: float, y: float, align_right: bool = ...) -> None:``

                 Set position of the 2D label.

                 Args:
                    x,y: label 2D position in screen coords ``[0..1]``
                    align_right: if ``True`` - anchor point of the label is the right side
                    align_top: if ``True`` - anchor point of the label is the top side
             )")
        .def("set_scale", &viz::Label::set_scale, py::arg("scale"),
             R"(
             Set scaling factor of the label.

             Args:
                scale: text scale factor
         )")
        .def(
            "set_rgba",
            [](viz::Label& self, py::tuple rgba) {
                viz::vec4f ar{0.0, 0.0, 0.0, 1.0};
                tuple_to_float_array(ar, rgba);
                self.set_rgba(ar);
            },
            py::arg("rgba"),
            R"(
            Set the color of the label.

            Args:
                rgba: 4 value tuple of RGBA color
        )");

    m.attr("spezia_palette") = py::array_t<float>{
        {static_cast<pysize>(viz::spezia_n), static_cast<pysize>(3)},
        &viz::spezia_palette[0][0]};
    m.attr("calref_palette") = py::array_t<float>{
        {static_cast<pysize>(viz::calref_n), static_cast<pysize>(3)},
        &viz::calref_palette[0][0]};

    m.attr("__version__") = ouster::SDK_VERSION;

    return m.ptr();
}
