<?php
// $Id$

/**
 * @file
 * Admin page callbacks for the filter module.
 */

/**
 * Menu callback; Displays a list of all input formats and which
 * one is the default.
 *
 * @ingroup forms
 * @see filter_admin_overview_submit()
 */
function filter_admin_overview() {

  // Overview of all formats.
  $formats = filter_formats();
  $error = FALSE;

  $form = array('#tree' => TRUE);
  foreach ($formats as $id => $format) {
    $roles = array();
    foreach (user_roles() as $rid => $name) {
      // Prepare a roles array with roles that may access the filter.
      if (strstr($format->roles, ",$rid,")) {
        $roles[] = $name;
      }
    }
    $default = ($id == variable_get('filter_default_format', 1));
    $options[$id] = '';
    $form[$id]['name'] = array('#markup' => $format->name);
    $form[$id]['roles'] = array('#markup' => $default ? t('All roles may use default format') : ($roles ? implode(', ', $roles) : t('No roles may use this format')));
    $form[$id]['configure'] = array('#markup' => l(t('configure'), 'admin/settings/filters/' . $id));
    $form[$id]['delete'] = array('#markup' => $default ? '' : l(t('delete'), 'admin/settings/filters/delete/' . $id));
    $form[$id]['weight'] = array('#type' => 'weight', '#default_value' => $format->weight);
  }
  $form['default'] = array('#type' => 'radios', '#options' => $options, '#default_value' => variable_get('filter_default_format', 1));
  $form['submit'] = array('#type' => 'submit', '#value' => t('Save changes'));
  return $form;
}

function filter_admin_overview_submit($form, &$form_state) {
  // Process form submission to set the default format.
  if (is_numeric($form_state['values']['default'])) {
    drupal_set_message(t('Default format updated.'));
    variable_set('filter_default_format', $form_state['values']['default']);
  }
  foreach ($form_state['values'] as $id => $data) {
    if (is_array($data) && isset($data['weight'])) {
      // Only update if this is a form element with weight.
      db_query("UPDATE {filter_format} SET weight = %d WHERE format = %d", $data['weight'], $id);
    }
  }
  drupal_set_message(t('The input format ordering has been saved.'));
}

/**
 * Theme the admin overview form.
 *
 * @ingroup themeable
 */
function theme_filter_admin_overview($form) {
  $rows = array();
  foreach ($form as $id => $element) {
    if (isset($element['roles']) && is_array($element['roles'])) {
      $element['weight']['#attributes']['class'] = 'input-format-order-weight';
      $rows[] = array(
        'data' => array(
          check_plain($element['name']['#markup']),
          drupal_render($element['roles']),
          drupal_render($form['default'][$id]),
          drupal_render($element['weight']),
          drupal_render($element['configure']),
          drupal_render($element['delete']),
        ),
        'class' => 'draggable',
      );
      unset($form[$id]);
    }
  }
  $header = array(t('Name'), t('Roles'), t('Default'), t('Weight'), array('data' => t('Operations'), 'colspan' => 2));
  $output = theme('table', $header, $rows, array('id' => 'input-format-order'));
  $output .= drupal_render($form);

  drupal_add_tabledrag('input-format-order', 'order', 'sibling', 'input-format-order-weight');

  return $output;
}

/**
 * Menu callback; Display a filter format form.
 */
function filter_admin_format_page($format = NULL) {
  if (!isset($format->name)) {
    drupal_set_title(t('Add input format'), PASS_THROUGH);
    $format = (object)array('name' => '', 'roles' => '', 'format' => '');
  }
  return drupal_get_form('filter_admin_format_form', $format);
}

/**
 * Generate a filter format form.
 *
 * @ingroup forms
 * @see filter_admin_format_form_validate()
 * @see filter_admin_format_form_submit()
 */
function filter_admin_format_form(&$form_state, $format) {
  $default = ($format->format == variable_get('filter_default_format', 1));
  if ($default) {
    $help = t('All roles for the default format must be enabled and cannot be changed.');
    $form['default_format'] = array('#type' => 'hidden', '#value' => 1);
  }

  $form['name'] = array('#type' => 'textfield',
    '#title' => t('Name'),
    '#default_value' => $format->name,
    '#description' => t('Specify a unique name for this filter format.'),
    '#required' => TRUE,
  );

  // Add a row of checkboxes for form group.
  $form['roles'] = array('#type' => 'fieldset',
    '#title' => t('Roles'),
    '#description' => $default ? $help : t('Choose which roles may use this filter format. Note that roles with the "administer filters" permission can always use all the filter formats.'),
    '#tree' => TRUE,
  );

  foreach (user_roles() as $rid => $name) {
    $checked = strstr($format->roles, ",$rid,");
    $form['roles'][$rid] = array('#type' => 'checkbox',
      '#title' => $name,
      '#default_value' => ($default || $checked),
    );
    if ($default) {
      $form['roles'][$rid]['#disabled'] = TRUE;
    }
  }
  // Table with filters
  $all = filter_list_all();
  $enabled = filter_list_format($format->format);

  $form['filters'] = array('#type' => 'fieldset',
    '#title' => t('Filters'),
    '#description' => t('Choose the filters that will be used in this filter format.'),
    '#tree' => TRUE,
  );
  foreach ($all as $id => $filter) {
    $form['filters'][$id] = array('#type' => 'checkbox',
      '#title' => $filter->name,
      '#default_value' => isset($enabled[$id]),
      '#description' => module_invoke($filter->module, 'filter', 'description', $filter->delta),
    );
  }
  if (!empty($format->format)) {
    $form['format'] = array('#type' => 'hidden', '#value' => $format->format);

    // Composition tips (guidelines)
    $tips = _filter_tips($format->format, FALSE);
    $tiplist = theme('filter_tips', $tips, FALSE);
    if (!$tiplist) {
      $tiplist = '<p>' . t('No guidelines available.') . '</p>';
    }
    else {
      $tiplist .= theme('filter_tips_more_info');
    }
    $group = '<p>' . t('These are the guidelines that users will see for posting in this input format. They are automatically generated from the filter settings.') . '</p>';
    $group .= $tiplist;
    $form['tips'] = array('#markup' => '<h2>' . t('Formatting guidelines') . '</h2>' . $group);
  }
  $form['submit'] = array('#type' => 'submit', '#value' => t('Save configuration'));

  return $form;
}

/**
 * Validate filter format form submissions.
 */
function filter_admin_format_form_validate($form, &$form_state) {
  if (!isset($form_state['values']['format'])) {
    $name = trim($form_state['values']['name']);
    $result = db_fetch_object(db_query("SELECT format FROM {filter_format} WHERE name='%s'", $name));
    if ($result) {
      form_set_error('name', t('Filter format names need to be unique. A format named %name already exists.', array('%name' => $name)));
    }
  }
}

/**
 * Process filter format form submissions.
 */
function filter_admin_format_form_submit($form, &$form_state) {
  $format = isset($form_state['values']['format']) ? $form_state['values']['format'] : NULL;
  $current = filter_list_format($format);
  $name = trim($form_state['values']['name']);
  $cache = TRUE;

  // Add a new filter format.
  if (!$format) {
    $new = TRUE;
    db_query("INSERT INTO {filter_format} (name) VALUES ('%s')", $name);
    $format = db_result(db_query("SELECT MAX(format) AS format FROM {filter_format}"));
    drupal_set_message(t('Added input format %format.', array('%format' => $name)));
  }
  else {
    drupal_set_message(t('The input format settings have been updated.'));
  }

  db_query("DELETE FROM {filter} WHERE format = %d", $format);
  foreach ($form_state['values']['filters'] as $id => $checked) {
    if ($checked) {
      list($module, $delta) = explode('/', $id);
      // Add new filters to the bottom.
      $weight = isset($current[$id]->weight) ? $current[$id]->weight : 10;
      db_query("INSERT INTO {filter} (format, module, delta, weight) VALUES (%d, '%s', %d, %d)", $format, $module, $delta, $weight);

      // Check if there are any 'no cache' filters.
      $cache &= !module_invoke($module, 'filter', 'no cache', $delta);
    }
  }

  // We store the roles as a string for ease of use.
  // We should always set all roles to TRUE when saving a default role.
  // We use leading and trailing comma's to allow easy substring matching.
  $roles = array();
  if (isset($form_state['values']['roles'])) {
    foreach ($form_state['values']['roles'] as $id => $checked) {
      if ($checked) {
        $roles[] = $id;
      }
    }
  }
  if (!empty($form_state['values']['default_format'])) {
    $roles = ',' . implode(',', array_keys(user_roles())) . ',';
  }
  else {
    $roles = ',' . implode(',', $roles) . ',';
  }

  db_query("UPDATE {filter_format} SET cache = %d, name='%s', roles = '%s' WHERE format = %d", $cache, $name, $roles, $format);

  cache_clear_all($format . ':', 'cache_filter', TRUE);

  // If a new filter was added, return to the main list of filters. Otherwise, stay on edit filter page to show new changes.
  $return = 'admin/settings/filters';
  if (!empty($new)) {
    $return .= '/' . $format;
  }
  $form_state['redirect'] = $return;
  return;
}

/**
 * Menu callback; confirm deletion of a format.
 *
 * @ingroup forms
 * @see filter_admin_delete_submit()
 */
function filter_admin_delete() {
  $format = arg(4);
  $format = db_fetch_object(db_query('SELECT * FROM {filter_format} WHERE format = %d', $format));

  if ($format) {
    if ($format->format != variable_get('filter_default_format', 1)) {
      $form['format'] = array('#type' => 'hidden', '#value' => $format->format);
      $form['name'] = array('#type' => 'hidden', '#value' => $format->name);

      return confirm_form($form, t('Are you sure you want to delete the input format %format?', array('%format' => $format->name)), 'admin/settings/filters', t('If you have any content left in this input format, it will be switched to the default input format. This action cannot be undone.'), t('Delete'), t('Cancel'));
    }
    else {
      drupal_set_message(t('The default format cannot be deleted.'));
      drupal_goto('admin/settings/filters');
    }
  }
  else {
    drupal_not_found();
  }
}

/**
 * Process filter delete form submission.
 */
function filter_admin_delete_submit($form, &$form_state) {
  db_query("DELETE FROM {filter_format} WHERE format = %d", $form_state['values']['format']);
  db_query("DELETE FROM {filter} WHERE format = %d", $form_state['values']['format']);

  $default = variable_get('filter_default_format', 1);
  // Replace existing instances of the deleted format with the default format.
  db_query("UPDATE {node_revision} SET format = %d WHERE format = %d", $default, $form_state['values']['format']);
  db_query("UPDATE {comment} SET format = %d WHERE format = %d", $default, $form_state['values']['format']);
  db_query("UPDATE {box} SET format = %d WHERE format = %d", $default, $form_state['values']['format']);

  cache_clear_all($form_state['values']['format'] . ':', 'cache_filter', TRUE);
  drupal_set_message(t('Deleted input format %format.', array('%format' => $form_state['values']['name'])));

  $form_state['redirect'] = 'admin/settings/filters';
  return;
}


/**
 * Menu callback; display settings defined by a format's filters.
 */
function filter_admin_configure_page($format) {
  drupal_set_title(t("Configure %format", array('%format' => $format->name)), PASS_THROUGH);
  return drupal_get_form('filter_admin_configure', $format);
}

/**
 * Build a form to change the settings for a format's filters.
 *
 * @ingroup forms
 */
function filter_admin_configure(&$form_state, $format) {
  $list = filter_list_format($format->format);
  $form = array();
  foreach ($list as $filter) {
    $form_module = module_invoke($filter->module, 'filter', 'settings', $filter->delta, $format->format);
    if (isset($form_module) && is_array($form_module)) {
      $form = array_merge($form, $form_module);
    }
  }

  if (!empty($form)) {
    $form = system_settings_form($form);
  }
  else {
    $form['error'] = array('#markup' => t('No settings are available.'));
  }
  $form['format'] = array('#type' => 'hidden', '#value' => $format->format);
  $form['#submit'][] = 'filter_admin_configure_submit';
  return $form;
}

/**
 * Clear the filter's cache when configuration settings are saved.
 */
function filter_admin_configure_submit($form, &$form_state) {
  cache_clear_all($form_state['values']['format'] . ':', 'cache_filter', TRUE);
}

/**
 * Menu callback; display form for ordering filters for a format.
 */
function filter_admin_order_page($format) {
  drupal_set_title(t("Rearrange %format", array('%format' => $format->name)), PASS_THROUGH);
  return drupal_get_form('filter_admin_order', $format);
}

/**
 * Build the form for ordering filters for a format.
 *
 * @ingroup forms
 * @see theme_filter_admin_order()
 * @see filter_admin_order_submit()
 */
function filter_admin_order(&$form_state, $format = NULL) {
  // Get list (with forced refresh).
  $filters = filter_list_format($format->format);

  $form['weights'] = array('#tree' => TRUE);
  foreach ($filters as $id => $filter) {
    $form['names'][$id] = array('#markup' => $filter->name);
    $form['weights'][$id] = array('#type' => 'weight', '#default_value' => $filter->weight);
  }
  $form['format'] = array('#type' => 'hidden', '#value' => $format->format);
  $form['submit'] = array('#type' => 'submit', '#value' => t('Save configuration'));

  return $form;
}

/**
 * Theme filter order configuration form.
 *
 * @ingroup themeable
 */
function theme_filter_admin_order($form) {
  $header = array(t('Name'), t('Weight'));
  $rows = array();
  foreach (element_children($form['names']) as $id) {
    // Don't take form control structures.
    if (is_array($form['names'][$id])) {
      $form['weights'][$id]['#attributes']['class'] = 'filter-order-weight';
      $rows[] = array(
        'data' => array(drupal_render($form['names'][$id]), drupal_render($form['weights'][$id])),
        'class' => 'draggable',
      );
    }
  }

  $output = theme('table', $header, $rows, array('id' => 'filter-order'));
  $output .= drupal_render($form);

  drupal_add_tabledrag('filter-order', 'order', 'sibling', 'filter-order-weight', NULL, NULL, FALSE);

  return $output;
}

/**
 * Process filter order configuration form submission.
 */
function filter_admin_order_submit($form, &$form_state) {
  foreach ($form_state['values']['weights'] as $id => $weight) {
    list($module, $delta) = explode('/', $id);
    db_query("UPDATE {filter} SET weight = %d WHERE format = %d AND module = '%s' AND delta = %d", $weight, $form_state['values']['format'], $module, $delta);
  }
  drupal_set_message(t('The filter ordering has been saved.'));

  cache_clear_all($form_state['values']['format'] . ':', 'cache_filter', TRUE);
}
