/*!
 * @file
 *
 * @section LICENSE
 *
 * Copyright (C) 2017 by the Georgia Tech Research Institute (GTRI)
 *
 * This file is part of SCRIMMAGE.
 *
 *   SCRIMMAGE is free software: you can redistribute it and/or modify it under
 *   the terms of the GNU Lesser General Public License as published by the
 *   Free Software Foundation, either version 3 of the License, or (at your
 *   option) any later version.
 *
 *   SCRIMMAGE is distributed in the hope that it will be useful, but WITHOUT
 *   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 *   License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with SCRIMMAGE.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @author Kevin DeMarco <kevin.demarco@gtri.gatech.edu>
 * @author Eric Squires <eric.squires@gtri.gatech.edu>
 * @date 31 July 2017
 * @version 0.1.0
 * @brief Brief file description.
 * @section DESCRIPTION
 * A Long description goes here.
 *
 */

#include <memory>
#include <limits>
#include <iostream>

#include <scrimmage/plugin_manager/RegisterPlugin.h>
#include <scrimmage/entity/Entity.h>
#include <scrimmage/common/Utilities.h>
#include <scrimmage/math/State.h>
#include <scrimmage/parse/ParseUtils.h>

#include <scrimmage/plugins/interaction/MapGen2D/MapGen2D.h>

#include <scrimmage/proto/ProtoConversions.h>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

using std::cout;
using std::endl;

namespace sc = scrimmage;

REGISTER_PLUGIN(scrimmage::EntityInteraction, MapGen2D, MapGen2D_plugin)

MapGen2D::MapGen2D()
{     
}

bool MapGen2D::init(std::map<std::string,std::string> &mission_params,
                    std::map<std::string,std::string> &plugin_params)
{

    show_map_debug_ = sc::get<bool>("show_map_debug", plugin_params,
                                    false);    
    
    ///////////////////////////////
    // Find the map params
    sc::ConfigParse map_parse;
    map_parse.set_required("filename");
    map_parse.set_required("resolution");
    
    sc::FileSearch file_search;
    std::map<std::string, std::string> overrides; // empty, no overrides
    if (!map_parse.parse(overrides, plugin_params["map"],
                            "SCRIMMAGE_DATA_PATH", file_search)) {        
        cout << "Failed to find map: " << plugin_params["map"] << endl;
        return false;
    }

    resolution_ = sc::get<double>("resolution", map_parse.params(), 1.0);
    wall_bottom_z_ = sc::get<double>("wall_bottom_z", map_parse.params(), 0.0);
    wall_height_ = sc::get<double>("wall_height", map_parse.params(), 5.0);
    enable_map_boundary_ = sc::get<bool>("enable_map_boundary",
                                         map_parse.params(), false);

    // Parse wall_color (default to blue)
    std::string color_str = sc::get<std::string>("wall_color",
                                                 map_parse.params(),
                                                 "0 0 255");

    // Parse wall color
    std::vector<int> color;
    bool color_status = sc::str2vec(color_str, " ", color, 3);
    if (!color_status) {
        cout << "Warning: Failed to parse wall color." << endl;
        color = {0, 0, 255};
    }
                
    std::string filename = map_parse.params()["XML_DIR"] + "/" +
        map_parse.params()["filename"];
    
    cv::Mat img = cv::imread(filename, CV_LOAD_IMAGE_COLOR);
    if (!img.data) {
        cout << "Failed to open file: " << filename << endl;
        return false;
    }

    std::list<cv::Rect> rects = find_rectangles(img, 150);  
   
    for (cv::Rect rect : rects) {
        double x = rect.x * resolution_;
        double y = (img.rows - rect.y) * resolution_;
        double width = rect.width * resolution_;
        double height = rect.height * resolution_;
        
        // Convert rectangle into cube shape
        Eigen::Vector3d center (x + width/2.0,
                                y - height/2.0,
                                wall_bottom_z_ + wall_height_ /2.0);
        
        sc::Quaternion quat(0,0,0);
    
        std::shared_ptr<sp::Shape> wall(new sp::Shape);
        wall->set_type(sp::Shape::Cube);
        sc::set(wall->mutable_color(), color[0], color[1], color[2]);
        wall->set_opacity(1.0);
        wall->set_persistent(true);
        sc::set(wall->mutable_center(), center);       
        sc::set(wall->mutable_xyz_lengths(), width, height, wall_height_);
        sc::set(wall->mutable_quat(), quat);        
                            
        shapes_.push_back(wall);
    }    
    
    return true;
}

bool MapGen2D::step_entity_interaction(std::list<sc::EntityPtr> &ents, 
                                                  double t, double dt)
{
    if (ents.empty())
    {
        return true;
    }

    return true;
}

std::list<cv::Rect> MapGen2D::find_rectangles(cv::Mat &img, int threshold)
{
    // Make sure image is gray and apply threshold
    cv::Mat gray;
    cv::cvtColor(img, gray, CV_BGR2GRAY);    

    cv::Mat thresh;
    cv::threshold(gray, thresh, threshold, 255, cv::THRESH_BINARY_INV);    

    cv::Mat img_rects = img.clone();
    
    std::list<cv::Rect> rects;

    cv::Mat I = thresh.clone();

    // Scan image looking for non-zero pixel
    for (int i = 0; i < I.rows; i++) {
        for (int j = 0; j < I.cols; j++) {            
            if (I.at<uchar>(i,j) > 0) {
                // Non-zero pixel found, search for largest rectangle of
                // non-zero pixels
                
                bool end_c_found = false;
                int end_c = I.cols;
                int end_r = I.rows;

                // Scan rows and columns looking for a zero-valued pixel
                for (int r = i; r < end_r; r++) {
                    for (int c = j; c < end_c; c++) {                        
                        if (I.at<uchar>(r,c) == 0) {
                            // Zero pixel found
                            if (!end_c_found) {
                                // Use this column number as end column of
                                // rectangle
                                end_c_found = true;
                                end_c = c;
                            } else {
                                // Use this row number as end row of rectangle
                                end_r = r;
                            }
                        }
                    }
                    
                    if (end_c == I.cols) {
                        // If end_c is still the same as I.cols, then the
                        // rectangle extended to the end of the image.
                        end_c_found = true;
                    }
                }

                cv::Rect rect(j, i, end_c-j, end_r-i);
                cv::rectangle(img_rects, rect, cv::Scalar(0,0,255), 1, 8, 0);
                cv::Mat roi = I(rect);
                roi.setTo(0);

                rects.push_back(rect);
            }
        }
    }

    if (enable_map_boundary_) {        
        rects.push_back(cv::Rect(0,0,img.cols,1)); //top rect
        rects.push_back(cv::Rect(0,0,1,img.rows)); //left rect
        rects.push_back(cv::Rect(img.cols,0,1,img.rows)); //right rect
        rects.push_back(cv::Rect(0,img.rows,img.cols,1)); //bottom rect        
    }
    

    if (show_map_debug_) {
        cout << "Number of rectangles: " << rects.size() << endl;
        cv::imshow("Original", img);
        cv::imshow("Gray", gray);
        cv::imshow("Thresh", thresh);
        cv::imshow("Rects", img_rects);
        cv::waitKey(0);
    }        
    return rects;
}
