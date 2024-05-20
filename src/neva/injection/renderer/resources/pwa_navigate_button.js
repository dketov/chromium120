// Copyright 2024 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0
{
  function do_back() {
    document.getElementById('back_button').style.scale = '1';
    window.history.back();
  }

  function do_forward() {
    document.getElementById('forward_button').style.scale = '1';
    window.history.forward();
  }

  function blockButton() {
    if (timerId_hideButton != 0) {
      clearTimeout(timerId_hideButton);
      timerId_hideButton = 0;
    }
    document.getElementById('back_button').style.backgroundImage = svgBack_block;
    document.getElementById('forward_button').style.backgroundImage = svgForward_block;
  }

  function goBack() {
    if (window.navigation.canGoBack) {
      document.getElementById('back_button').style.scale = '0.6';
      blockButton();
      setTimeout(do_back, 300);
    }
  }

  function goForward() {
    if (window.navigation.canGoForward) {
      document.getElementById('forward_button').style.scale = '0.6';
      blockButton();
      setTimeout(do_forward, 300);
    }
  }

  function hideButtons() {
    document.getElementById('back_button').style.visibility = 'hidden';
    document.getElementById('forward_button').style.visibility = 'hidden';
    timerId_hideButton = 0;
  }

  function on_touchstart(e) {
    touchmove_y = e.touches[0].clientY;
  }

  function on_touchend(e) {
    touchmove_y = e.changedTouches[0].clientY - touchmove_y;
    if (touchmove_y > delta_for_show) {
      showButtons();
    }
    touchmove_y = 0;
  }

  function on_wheel(e) {
    if (e.wheelDelta >= 120) {
      showButtons();
    }
  }

  function showButtons() {
    checkButtonActive(document.getElementById('back_button'), window.navigation.canGoBack, back_cssText, svgBack_active, svgBack_block);
    checkButtonActive(document.getElementById('forward_button'), window.navigation.canGoForward, forward_cssText, svgForward_active, svgForward_block);

    document.getElementById('back_button').style.visibility = 'visible';
    document.getElementById('forward_button').style.visibility = 'visible';

    if (timerId_hideButton != 0)
      clearTimeout(timerId_hideButton);

    timerId_hideButton = setTimeout(hideButtons, 5000);
  }

  function checkButtonActive(button, isActive, cssText, svgActiv, svgBlock) {
    //button.style.all = 'unset';
    button.style.cssText = cssText;
    if (isActive) {
      button.style.backgroundImage = svgActiv;
      button.style.opacity = '0.8';
      button.style.scale = '1';
    } else {
      button.style.backgroundImage = svgBlock;
      button.style.opacity = '0.5';
      button.style.scale = '0.8';
    }
  }


  function createBackButton() {
    const back_button = document.createElement('button');
    back_button.onmouseover = function () {
      if (window.navigation.canGoBack)
        back_button.style.scale = '0.8';
    };

    back_button.onmouseleave = function () {
      if (window.navigation.canGoBack)
        back_button.style.scale = '1';
    };

    back_button.addEventListener('click', goBack);

    document.body.append(back_button);
    back_button.id = 'back_button';
  }

  function createForwardButton() {
    const forward_button = document.createElement('button');
    forward_button.onmouseover = function () {
      if (window.navigation.canGoForward)
        forward_button.style.scale = '0.8';
    };

    forward_button.onmouseleave = function () {
      if (window.navigation.canGoForward)
        forward_button.style.scale = '1';
    };

    document.body.append(forward_button);
    forward_button.addEventListener('click', goForward);
    forward_button.id = 'forward_button';
  }

  function createModel() {
    navigation.addEventListener("navigatesuccess", (event) => {
      hideButtons();
    });

    window.addEventListener('touchstart', on_touchstart);
    window.addEventListener('touchend', on_touchend);
    window.addEventListener('wheel', on_wheel);

    createBackButton();
    createForwardButton();

    hideButtons();
  }
  console.log('Load pwa_navigate_buttons.js!');
  let svgBack_active =
    `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 330 330' width='800px' height='800px' fill='%23000000'%3E%3Cpath style='fill:%23445500' d='m 185.09757,70.848847 c 5.857,-5.857 15.355,-5.858 21.213,10e-4 5.858,5.858 5.858,15.355 0,21.213 l -69.393,69.391993 69.393,69.395 c 5.858,5.858 5.858,15.355 0,21.213 -2.929,2.929 -6.768,4.393 -10.607,4.393 -3.839,0 -7.678,-1.464 -10.606,-4.394 l -80,-80.002 c -2.814,-2.813 -4.394,-6.628 -4.394,-10.607 0,-3.979 1.58,-7.794 4.394,-10.607 z' /%3E%3C/svg%3E")`;
  let svgBack_block =
    `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 330 330' width='800px' height='800px' fill='%234d4d4d'%3E%3Cpath style='fill:%23808080' d='m 185.09757,70.848847 c 5.857,-5.857 15.355,-5.858 21.213,10e-4 5.858,5.858 5.858,15.355 0,21.213 l -69.393,69.391993 69.393,69.395 c 5.858,5.858 5.858,15.355 0,21.213 -2.929,2.929 -6.768,4.393 -10.607,4.393 -3.839,0 -7.678,-1.464 -10.606,-4.394 l -80,-80.002 c -2.814,-2.813 -4.394,-6.628 -4.394,-10.607 0,-3.979 1.58,-7.794 4.394,-10.607 z' /%3E%3C/svg%3E")`;
  let svgForward_active =
    `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 330 330' width='800px' height='800px' fill='%23000000'%3E%3Cpath style='fill:%23445500' id='path56' d='m 145.606,255.607 c -5.857,5.857 -15.355,5.858 -21.213,-0.001 -5.858,-5.858 -5.858,-15.355 0,-21.213 l 69.393,-69.392 -69.393,-69.395 c -5.858,-5.858 -5.858,-15.355 0,-21.213 C 127.322,71.464 131.161,70 135,70 c 3.839,0 7.678,1.464 10.606,4.394 l 80,80.002 c 2.814,2.813 4.394,6.628 4.394,10.607 0,3.979 -1.58,7.794 -4.394,10.607 z' /%3E%3C/svg%3E")`;
  let svgForward_block =
    `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 330 330' width='800px' height='800px' fill='%234d4d4d'%3E%3Cpath style='fill:%23808080' id='path56' d='m 145.606,255.607 c -5.857,5.857 -15.355,5.858 -21.213,-0.001 -5.858,-5.858 -5.858,-15.355 0,-21.213 l 69.393,-69.392 -69.393,-69.395 c -5.858,-5.858 -5.858,-15.355 0,-21.213 C 127.322,71.464 131.161,70 135,70 c 3.839,0 7.678,1.464 10.606,4.394 l 80,80.002 c 2.814,2.813 4.394,6.628 4.394,10.607 0,3.979 -1.58,7.794 -4.394,10.607 z' /%3E%3C/svg%3E")`;
  let back_cssText =
    'width: 80px; height: 80px; border: none; background-size: cover;  background-position: center; position: fixed; left: 15px; top: 10px; z-index: 99999; border-radius: 50%;';
  let forward_cssText =
    'width: 80px; height: 80px; border: none; background-size: cover;  background-position: center; position: fixed; left: 115px; top: 10px; z-index: 99999; border-radius: 50%;';

  const delta_for_show = 50;
  let touchmove_y = 0;
  let timerId_hideButton = 0;

  if (document.getElementById('back_button') == null)
    createModel();
}