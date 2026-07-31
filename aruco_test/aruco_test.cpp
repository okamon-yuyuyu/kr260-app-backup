#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>
#include <iostream>
#include <cmath>
#include <cstdio>

int main()
{
    const int camera_id = 0;
    const double marker_length = 0.05; // [m] 5.0cm

    cv::Mat camera_matrix, dist_coeffs;

    cv::FileStorage fs("/home/ubuntu/yaml/camera_many.yaml", cv::FileStorage::READ);
    if (!fs.isOpened()) {
        std::cerr << "camera_many.yaml が開けません" << std::endl;
        return -1;
    }

    fs["camera_matrix"] >> camera_matrix;
    fs["dist_coeffs"] >> dist_coeffs;
    fs.release();

    cv::VideoCapture cap(camera_id, cv::CAP_V4L2);
    if (!cap.isOpened()) {
        std::cerr << "カメラを開けません" << std::endl;
        return -1;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    auto dictionary = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);

    std::vector<cv::Point3f> object_points;
    float L = marker_length / 2.0f;

    object_points.push_back(cv::Point3f(-L,  L, 0)); // 左上
    object_points.push_back(cv::Point3f( L,  L, 0)); // 右上
    object_points.push_back(cv::Point3f( L, -L, 0)); // 右下
    object_points.push_back(cv::Point3f(-L, -L, 0)); // 左下

    cv::Mat frame;

    while (true) {
        cap >> frame;
        if (frame.empty()) break;

        cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);

        std::vector<int> ids;
        std::vector<std::vector<cv::Point2f>> corners;

        cv::aruco::detectMarkers(frame, dictionary, corners, ids);

        if (!ids.empty()) {
            cv::aruco::drawDetectedMarkers(frame, corners, ids);

            for (size_t i = 0; i < ids.size(); i++) {
                cv::Mat rvec, tvec;

                bool ok = cv::solvePnP(
                    object_points,
                    corners[i],
                    camera_matrix,
                    dist_coeffs,
                    rvec,
                    tvec
                );

                if (ok) {
                    double x = tvec.at<double>(0);
                    double y = tvec.at<double>(1);
                    double z = tvec.at<double>(2);

                    double distance = std::sqrt(x*x + y*y + z*z);
                    double angle_deg = std::atan2(x, z) * 180.0 / M_PI;

                    double rx = rvec.at<double>(0);
                    double ry = rvec.at<double>(1);
                    double rz = rvec.at<double>(2);

                    cv::Mat R;
                    cv::Rodrigues(rvec, R);

                    double sy = std::sqrt(
                        R.at<double>(0,0) * R.at<double>(0,0) +
                        R.at<double>(1,0) * R.at<double>(1,0)
                    );

                    bool singular = sy < 1e-6;

                    double roll, pitch, yaw;

                    if (!singular) {
                        roll  = std::atan2(R.at<double>(2,1), R.at<double>(2,2));
                        pitch = std::atan2(-R.at<double>(2,0), sy);
                        yaw   = std::atan2(R.at<double>(1,0), R.at<double>(0,0));
                    } else {
                        roll  = std::atan2(-R.at<double>(1,2), R.at<double>(1,1));
                        pitch = std::atan2(-R.at<double>(2,0), sy);
                        yaw   = 0;
                    }

                    roll  = roll  * 180.0 / M_PI;
                    pitch = pitch * 180.0 / M_PI;
                    yaw   = yaw   * 180.0 / M_PI;

                    std::cout << "ID: " << ids[i]
                              << "  X: " << x * 1000.0 << " mm"
                              << "  Y: " << y * 1000.0 << " mm"
                              << "  Z: " << z * 1000.0 << " mm"
                              << "  distance: " << distance * 1000.0 << " mm"
                              << "  angle: " << angle_deg << " deg"
                              << "  rvec: [" << rx << ", " << ry << ", " << rz << "]"
                              << "  Roll: " << roll
                              << "  Pitch: " << pitch
                              << "  Yaw: " << yaw
                              << std::endl;

                    cv::drawFrameAxes(
                        frame,
                        camera_matrix,
                        dist_coeffs,
                        rvec,
                        tvec,
                        marker_length * 0.5
                    );

                    char text1[100];
                    char text2[100];
                    char text3[100];
                    char text4[100];

                    sprintf(text1,
                            "X=%.0f Y=%.0f Z=%.0f mm",
                            x * 1000.0,
                            y * 1000.0,
                            z * 1000.0);

                    sprintf(text2,
                            "Angle=%.1f deg  Dist=%.0f mm",
                            angle_deg,
                            distance * 1000.0);

                    sprintf(text3,
                            "rvec=[%.2f %.2f %.2f]",
                            rx, ry, rz);

                    sprintf(text4,
                            "Roll=%.1f Pitch=%.1f Yaw=%.1f deg",
                            roll, pitch, yaw);

                    cv::putText(frame, text1,
                                cv::Point(20, 40),
                                cv::FONT_HERSHEY_SIMPLEX,
                                0.8,
                                cv::Scalar(0, 255, 0),
                                2);

                    cv::putText(frame, text2,
                                cv::Point(20, 80),
                                cv::FONT_HERSHEY_SIMPLEX,
                                0.8,
                                cv::Scalar(0, 255, 0),
                                2);

                    cv::putText(frame, text3,
                                cv::Point(20, 120),
                                cv::FONT_HERSHEY_SIMPLEX,
                                0.8,
                                cv::Scalar(0, 255, 255),
                                2);

                    cv::putText(frame, text4,
                                cv::Point(20, 160),
                                cv::FONT_HERSHEY_SIMPLEX,
                                0.8,
                                cv::Scalar(0, 255, 255),
                                2);
                }
            }
        }

        cv::imshow("ArUco distance", frame);

        if (cv::waitKey(1) == 'q') {
            break;
        }
    }

    cap.release();
    cv::destroyAllWindows();

    return 0;
}