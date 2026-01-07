/**
 * wshowlyrics GitHub Pages - Main JavaScript
 */

(function() {
  'use strict';

  // Mobile Navigation Toggle
  const navToggle = document.querySelector('.nav-toggle');
  const navLinks = document.querySelector('.nav-links');

  if (navToggle && navLinks) {
    navToggle.addEventListener('click', function() {
      navLinks.classList.toggle('active');
    });

    // Close nav when clicking a link
    navLinks.querySelectorAll('a').forEach(function(link) {
      link.addEventListener('click', function() {
        navLinks.classList.remove('active');
      });
    });
  }

  // Installation Tabs with smooth height transition
  const tabButtons = document.querySelectorAll('.tab-btn');
  const tabContents = document.querySelectorAll('.tab-content');
  const tabContainer = document.querySelector('.tab-container');

  // Set initial height
  if (tabContainer) {
    var activeTab = tabContainer.querySelector('.tab-content.active');
    if (activeTab) {
      tabContainer.style.height = activeTab.offsetHeight + 'px';
    }
  }

  tabButtons.forEach(function(button) {
    button.addEventListener('click', function() {
      const tabId = this.getAttribute('data-tab');
      const newContent = document.getElementById(tabId);

      // Remove active class from all buttons
      tabButtons.forEach(function(btn) {
        btn.classList.remove('active');
      });

      // Get current height
      const currentHeight = tabContainer.offsetHeight;
      tabContainer.style.height = currentHeight + 'px';

      // Hide all contents
      tabContents.forEach(function(content) {
        content.classList.remove('active');
      });

      // Show new content and animate height
      this.classList.add('active');
      newContent.classList.add('active');

      // Calculate new height and animate
      requestAnimationFrame(function() {
        const newHeight = newContent.offsetHeight;
        tabContainer.style.height = newHeight + 'px';
      });
    });
  });

  // Handle window resize
  window.addEventListener('resize', function() {
    if (tabContainer) {
      var activeTab = tabContainer.querySelector('.tab-content.active');
      if (activeTab) {
        tabContainer.style.height = activeTab.offsetHeight + 'px';
      }
    }
  });

  // Header scroll effect
  const header = document.querySelector('.site-header');
  let lastScroll = 0;

  window.addEventListener('scroll', function() {
    const currentScroll = window.pageYOffset;

    if (currentScroll <= 0) {
      header.style.boxShadow = 'none';
    } else {
      header.style.boxShadow = '0 2px 10px rgba(0, 0, 0, 0.3)';
    }

    lastScroll = currentScroll;
  });

  // Platform-specific warning message
  var platformMessage = document.getElementById('platform-message');
  var platformLabel = document.getElementById('platform-label');
  var platformNotice = document.getElementById('platform-notice');
  if (platformMessage && platformLabel) {
    var ua = navigator.userAgent.toLowerCase();
    if (ua.indexOf('win') !== -1) {
      platformLabel.textContent = 'Warning:';
      platformLabel.style.color = '#f85149';
      platformNotice.style.borderLeftColor = '#f85149';
      platformNotice.style.background = 'rgba(248, 81, 73, 0.1)';
      platformMessage.textContent = 'This application is not supported on Windows. It requires Linux with Wayland.';
    } else if (ua.indexOf('mac') !== -1) {
      platformLabel.textContent = 'Warning:';
      platformLabel.style.color = '#f85149';
      platformNotice.style.borderLeftColor = '#f85149';
      platformNotice.style.background = 'rgba(248, 81, 73, 0.1)';
      platformMessage.textContent = 'This application is not supported on macOS. It requires Linux with Wayland.';
    } else {
      platformMessage.innerHTML = 'This application requires a Wayland compositor. See <a href="#compatibility" style="color: var(--color-accent);">compatibility</a> for details.';
    }
  }

  // Smooth scroll for anchor links (fallback for browsers without CSS scroll-behavior)
  document.querySelectorAll('a[href^="#"]').forEach(function(anchor) {
    anchor.addEventListener('click', function(e) {
      const targetId = this.getAttribute('href');
      if (targetId === '#') return;

      const targetElement = document.querySelector(targetId);
      if (targetElement) {
        e.preventDefault();
        targetElement.scrollIntoView({
          behavior: 'smooth',
          block: 'start'
        });
      }
    });
  });
})();
